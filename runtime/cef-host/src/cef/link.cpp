#include "link.hpp"

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <utility>
#include <vector>

#include "host.hpp"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_process_message.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace pico_desktop::host::cef {

namespace {

constexpr std::size_t kMaxMessage = 4 * 1024 * 1024;  // docs/protocol.md §2
constexpr int64_t kRestartGraceMs = 2000;

Link* g_link = nullptr;

std::string page_key(const std::string& frame_id, int context) { return frame_id + "#" + std::to_string(context); }

/// Reaps `pid`, waiting up to `grace` and then killing it; the wait status, or -1.
int reap(pid_t pid, std::chrono::milliseconds grace) {
  const auto until = std::chrono::steady_clock::now() + grace;
  int status = 0;
  for (;;) {
    const pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      return status;
    }
    if (r < 0 && errno != EINTR) {
      return -1;
    }
    if (std::chrono::steady_clock::now() >= until) {
      break;
    }
    poll(nullptr, 0, 10);
  }
  kill(pid, SIGKILL);
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      return -1;
    }
  }
  return status;
}

std::string describe(int status) {
  if (status < 0) {
    return "an unknown status";
  }
  if (WIFEXITED(status)) {
    return "exit code " + std::to_string(WEXITSTATUS(status));
  }
  if (WIFSIGNALED(status)) {
    return "signal " + std::to_string(WTERMSIG(status));
  }
  return "status " + std::to_string(status);
}

}  // namespace

Link& link() { return *g_link; }

void init_link(const Options& options) { g_link = new Link(options); }

Link::Link(const Options& options) : options_(options), router_(*this, options.mode == Mode::self_test) {
  // A backend that dies mid-write must not take the host down: writes fail with EPIPE instead.
  signal(SIGPIPE, SIG_IGN);
}

Link::~Link() { stop(std::chrono::milliseconds(0)); }

void Link::start() {
  CEF_REQUIRE_UI_THREAD();
  if (options_.backend.empty()) {
    log("pd-host: no --backend: the page has no backend");
    return;
  }
  spawn();
}

// ------------------------------------------------------------------------------------ pages

void Link::from_page(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int context,
                     std::span<const std::byte> message) {
  const std::string key = page_key(frame->GetIdentifier().ToString(), context);
  auto it = session_of_.find(key);
  if (it == session_of_.end()) {
    const std::uint64_t session = next_session_++;
    it = session_of_.emplace(key, session).first;
    pages_[session] = {browser->GetIdentifier(), frame->GetIdentifier().ToString(), context};
  }
  router_.from_session(it->second, static_cast<std::uint64_t>(browser->GetIdentifier()), message);
}

void Link::page_closed(const std::string& frame_id, int context) {
  const auto it = session_of_.find(page_key(frame_id, context));
  if (it == session_of_.end()) {
    return;  // a context that never sent anything
  }
  const std::uint64_t session = it->second;
  session_of_.erase(it);
  pages_.erase(session);
  router_.end_session(session);
}

void Link::browser_gone(int browser_id) {
  for (auto it = pages_.begin(); it != pages_.end();) {
    if (it->second.browser_id != browser_id) {
      ++it;
      continue;
    }
    const std::uint64_t session = it->first;
    session_of_.erase(page_key(it->second.frame_id, it->second.context));
    it = pages_.erase(it);
    router_.end_session(session);
  }
}

void Link::to_session(std::uint64_t session, Bytes message) {
  const auto it = pages_.find(session);
  CefRefPtr<CefBrowser> browser = browser_state().browser;
  if (it == pages_.end() || !browser || browser->GetIdentifier() != it->second.browser_id) {
    return;
  }
  CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(it->second.frame_id);
  if (!frame || !frame->IsValid()) {
    return;
  }
  CefRefPtr<CefProcessMessage> reply = CefProcessMessage::Create(kBridgeReply);
  CefRefPtr<CefListValue> args = reply->GetArgumentList();
  args->SetInt(0, it->second.context);
  args->SetBinary(1, CefBinaryValue::Create(message.data(), message.size()));
  frame->SendProcessMessage(PID_RENDERER, reply);
}

void Link::log(const std::string& line) { log_line(line); }

void Link::kill_backend() {
  if (process_) {
    log("pd-host: killing the backend (self-test)");
    kill(process_->child.pid, SIGKILL);
  }
}

// ---------------------------------------------------------------------------------- backend

void Link::spawn() {
  CEF_REQUIRE_UI_THREAD();
  if (closing_ || process_) {
    return;
  }
  std::string error;
  auto child = ipc::spawn_piped(options_.backend.string(), {}, error);
  if (!child) {
    log("pd-host: couldn't start the backend: " + error);
    schedule_restart();
    return;
  }
  process_ = std::make_unique<Process>();
  process_->child = *child;
  process_->generation = ++generation_;
  process_->reader = std::thread(read_loop, process_.get());
  process_->writer = std::thread(write_loop, process_.get());
  log("pd-host: started the backend (pid " + std::to_string(child->pid) + ")");
  router_.backend_started();
}

void Link::to_backend(Bytes message) {
  if (!process_) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(process_->mutex);
    process_->queue.push_back(std::move(message));
  }
  process_->wake.notify_one();
}

void Link::on_backend_message(std::uint64_t generation, Bytes message) {
  if (!process_ || process_->generation != generation) {
    return;  // from a process that's already gone
  }
  const bool was_ready = router_.backend_ready();
  router_.from_backend(message);
  if (!was_ready && router_.backend_ready()) {
    budget_.ready(RestartBudget::Clock::now());
    log("pd-host: the backend is ready (epoch " + std::to_string(router_.epoch()) + ")");
  }
}

void Link::on_backend_exit(std::uint64_t generation) {
  if (!process_ || process_->generation != generation) {
    return;
  }
  const int status = end_process(std::chrono::milliseconds(0));
  if (closing_) {
    return;
  }
  log("pd-host: the backend exited (" + describe(status) + ")");
  router_.backend_stopped();
  if (planned_restart_) {
    planned_restart_ = false;
    budget_ = RestartBudget();
    log("pd-host: starting the backend again (requested)");
    router_.announce("restarting");
    spawn();
    return;
  }
  schedule_restart();
}

void Link::restart_backend() {
  if (closing_ || options_.backend.empty() || planned_restart_) {
    return;
  }
  if (!process_) {
    // Failed, or waiting for a backoff: a rebuilt backend gets a fresh budget at once.
    budget_ = RestartBudget();
    spawn();
    return;
  }
  planned_restart_ = true;
  log("pd-host: restarting the backend (requested)");
  router_.backend_closing();
  CefPostDelayedTask(TID_UI, base::BindOnce(&Link::force_end, base::Unretained(this), process_->generation),
                     kRestartGraceMs);
}

void Link::force_end(std::uint64_t generation) {
  if (process_ && process_->generation == generation) {
    log("pd-host: the backend didn't finish in time; killing it");
    kill(process_->child.pid, SIGKILL);
  }
}

void Link::schedule_restart() {
  if (closing_) {
    return;
  }
  const auto delay = budget_.next(RestartBudget::Clock::now());
  if (!delay) {
    log("pd-host: the backend keeps exiting; not starting it again");
    router_.announce("failed");
    return;
  }
  log("pd-host: starting the backend again in " + std::to_string(delay->count()) + " ms");
  router_.announce("restarting");
  CefPostDelayedTask(TID_UI, base::BindOnce(&Link::spawn, base::Unretained(this)), delay->count());
}

void Link::closing() {
  if (closing_) {
    return;
  }
  router_.closing();
  closing_ = true;
}

void Link::stop(std::chrono::milliseconds grace) {
  closing_ = true;
  if (process_) {
    const int status = end_process(grace);
    log("pd-host: the backend ended (" + describe(status) + ")");
  }
}

int Link::end_process(std::chrono::milliseconds grace) {
  std::unique_ptr<Process> process = std::move(process_);
  {
    std::lock_guard<std::mutex> lock(process->mutex);
    process->draining = true;
  }
  process->wake.notify_one();
  // The writer closes stdin once the queue is out; a backend that doesn't exit then is killed,
  // which also unblocks a writer stuck on a full pipe.
  const int status = reap(process->child.pid, grace);
  process->writer.join();
  process->reader.join();
  close(process->child.from_child);
  return status;
}

void Link::read_loop(Process* process) {
  ipc::FrameBuffer frames(kMaxMessage);
  std::vector<std::byte> chunk(64 * 1024);
  const std::uint64_t generation = process->generation;
  for (;;) {
    const ssize_t n = ::read(process->child.from_child, chunk.data(), chunk.size());
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      break;
    }
    frames.push(std::span<const std::byte>(chunk.data(), static_cast<std::size_t>(n)));
    while (auto message = frames.next()) {
      CefPostTask(TID_UI, base::BindOnce(&Link::on_backend_message, base::Unretained(g_link), generation,
                                         std::move(*message)));
    }
    if (!frames.error().empty()) {
      CefPostTask(TID_UI, base::BindOnce(&Link::log, base::Unretained(g_link),
                                         "pd-host: the backend broke the framing: " + frames.error()));
      break;
    }
  }
  CefPostTask(TID_UI, base::BindOnce(&Link::on_backend_exit, base::Unretained(g_link), generation));
}

void Link::write_loop(Process* process) {
  bool broken = false;
  for (;;) {
    Bytes message;
    {
      std::unique_lock<std::mutex> lock(process->mutex);
      process->wake.wait(lock, [process] { return !process->queue.empty() || process->draining; });
      if (process->queue.empty()) {
        break;  // draining and nothing left
      }
      message = std::move(process->queue.front());
      process->queue.pop_front();
    }
    // After a failed write the backend is gone: the rest is dropped (the reader reports the exit).
    broken = broken || !ipc::write_all(process->child.to_child, ipc::frame(message));
  }
  close(process->child.to_child);
}

}  // namespace pico_desktop::host::cef
