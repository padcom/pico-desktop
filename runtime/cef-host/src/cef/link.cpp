#include "link.hpp"

#if !defined(_WIN32)
#include <poll.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <utility>
#include <vector>

#include "host.hpp"
#include "process_os.hpp"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_process_message.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "pico_desktop/host/services.hpp"
#include "pico_desktop/ipc/value.hpp"

namespace pico_desktop::host::cef {

namespace {

constexpr std::size_t kMaxMessage = 4 * 1024 * 1024;  // docs/protocol.md §2
constexpr int64_t kRestartGraceMs = 2000;
// The queue to the backend: beyond this, pages' new requests get BUSY (docs/protocol.md §7).
constexpr std::size_t kMaxQueuedMessages = 1024;
constexpr std::size_t kMaxQueuedBytes = 16 * 1024 * 1024;

Link* g_link = nullptr;

/// Stops `thread`'s pipe calls (os::interrupt_io); if the cancel hasn't taken after 2 s, says so in
/// the log and keeps at it (the thread must be out of its call before it can be joined).
void stop_pipe_io(std::thread& thread, InterruptibleIo& io, const char* what) {
  if (!os::interrupt_io(thread, io, std::chrono::seconds(2))) {
    log_line(std::string("pd-host: the backend's ") + what + " is still blocked 2 s after it was cancelled; still trying");
    os::interrupt_io(thread, io);
  }
}

std::string page_key(const std::string& frame_id, int context) { return frame_id + "#" + std::to_string(context); }

/// Reaps `pid`, waiting up to `grace` and then killing it; the wait status, or -1.
}  // namespace

void Link::trace(const char* direction, std::span<const std::byte> message) {
  if (options_.trace_ipc) {
    log(std::string("pd-ipc: ") + direction + " " + describe_envelope(message));
  }
}

Link& link() { return *g_link; }

void init_link(const Options& options) { g_link = new Link(options); }

Link::Link(const Options& options) : options_(options), router_(*this, options.mode == Mode::self_test) {
  // A backend that dies mid-write must not take the host down: writes fail with EPIPE instead.
  os::init_process_handling();
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
  trace("page → host   ", message);
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
  trace("host → page   ", message);
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
    os::kill(process_->child);
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
  process_->pidfd = os::exit_watch(*child);
#if !defined(_WIN32)
  if (!ipc::cloexec_pipe(process_->stop)) {
    process_->stop[0] = process_->stop[1] = -1;
  }
#endif
  os::prepare_pipes(*child);
  process_->reader = std::thread(read_loop, process_.get());
  process_->writer = std::thread(write_loop, process_.get());
  // Windows: the exit stops both, even while something the backend started holds its pipes (#17).
  process_->watcher = os::watch_exit(*child, [process = process_.get()] {
    stop_pipe_io(process->reader, process->reader_io, "stdout reader");
    stop_pipe_io(process->writer, process->writer_io, "stdin writer");
  });
  log("pd-host: started the backend (pid " + std::to_string(child->pid) + ")");
  router_.backend_started();
}

void Link::to_backend(Bytes message) {
  if (!process_) {
    return;
  }
  trace("host → backend", message);
  {
    std::lock_guard<std::mutex> lock(process_->mutex);
    process_->queued_bytes += message.size();
    process_->queue.push_back(std::move(message));
  }
  process_->wake.notify_one();
}

void Link::on_backend_message(std::uint64_t generation, Bytes message) {
  if (!process_ || process_->generation != generation) {
    return;  // from a process that's already gone
  }
  trace("backend → host", message);
  const bool was_ready = router_.backend_ready();
  router_.from_backend(message);
  if (!was_ready && router_.backend_ready()) {
    budget_.ready(RestartBudget::Clock::now());
    log("pd-host: the backend is ready (epoch " + std::to_string(router_.epoch()) + ")");
    if (options_.liveness_interval_ms > 0) {
      liveness_tick(generation);
    }
  }
}

void Link::on_backend_exit(std::uint64_t generation) {
  if (!process_ || process_->generation != generation) {
    return;
  }
  const os::Status status = end_process(std::chrono::milliseconds(0));
  if (closing_) {
    log("pd-host: the backend ended (" + os::describe(status) + ")");
    finish_close();
    return;
  }
  log("pd-host: the backend exited (" + os::describe(status) + ")");
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
    os::kill(process_->child);
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

void Link::liveness_tick(std::uint64_t generation) {
  if (!process_ || process_->generation != generation || closing_ || planned_restart_) {
    return;  // that backend is gone or on its way out
  }
  const auto silent = std::chrono::steady_clock::now() - process_->last_pong;
  if (process_->answered && silent > std::chrono::milliseconds(options_.liveness_timeout_ms)) {
    log("pd-host: the backend stopped answering (no pong for " +
        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(silent).count()) + " ms); killing it");
    os::kill(process_->child);  // then it's a crash: the restart policy takes over
    return;
  }
  router_.ping(++process_->pings);
  CefPostDelayedTask(TID_UI, base::BindOnce(&Link::liveness_tick, base::Unretained(this), generation),
                     options_.liveness_interval_ms);
}

void Link::pong() {
  if (process_) {
    process_->answered = true;
    process_->last_pong = std::chrono::steady_clock::now();
  }
}

bool Link::backend_busy() const {
  if (!process_) {
    return false;
  }
  std::lock_guard<std::mutex> lock(process_->mutex);
  return process_->queue.size() >= kMaxQueuedMessages || process_->queued_bytes >= kMaxQueuedBytes;
}

void Link::host_request(std::uint64_t id, const std::string& name, std::optional<Bytes> payload,
                        std::optional<std::uint64_t> window) {
  if (!process_) {
    return;
  }
  const ServiceCall call{id, window, process_->generation};
  const ServiceInfo info = service_info(name);
  if (!info.known) {
    refuse(call, "UNKNOWN_METHOD", "the host has no service " + name);
    return;
  }
  if (info.needs && !options_.capabilities.contains(*info.needs)) {
    refuse(call, "NOT_ALLOWED",
           name + " needs the '" + capability_name(*info.needs) + "' capability (pico-desktop.config.ts: capabilities)");
    return;
  }
  ipc::Value params;
  params.type = ipc::Value::Type::map;
  if (payload) {
    auto decoded = ipc::decode_value(*payload);
    if (!decoded) {
      refuse(call, "INVALID_PAYLOAD", "the parameters aren't valid CBOR");
      return;
    }
    params = std::move(*decoded);
  }
  run_service(call, name, params);
}

void Link::reply(const ServiceCall& call, std::optional<Bytes> payload) {
  if (process_ && process_->generation == call.generation) {
    router_.answer_backend(call.id, std::move(payload), call.window);
  }
}

void Link::refuse(const ServiceCall& call, const char* code, const std::string& message) {
  if (process_ && process_->generation == call.generation) {
    router_.fail_backend(call.id, code, message, call.window);
  }
}

void Link::begin_close(base::OnceClosure done) {
  if (close_done_ || closing_) {
    if (done) {
      std::move(done).Run();
    }
    return;
  }
  log("pd-host: closing: telling the page and the backend");
  router_.closing();
  closing_ = true;
  close_done_ = std::move(done);
  if (!process_) {
    finish_close();
    return;
  }
  CefPostDelayedTask(TID_UI, base::BindOnce(&Link::force_end, base::Unretained(this), process_->generation),
                     options_.close_grace_ms);
}

void Link::finish_close() {
  if (close_done_) {
    std::move(close_done_).Run();
  }
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
    const os::Status status = end_process(grace);
    log("pd-host: the backend ended (" + os::describe(status) + ")");
  }
}

os::Status Link::end_process(std::chrono::milliseconds grace) {
  std::unique_ptr<Process> process = std::move(process_);
  {
    std::lock_guard<std::mutex> lock(process->mutex);
    process->draining = true;
  }
  process->wake.notify_one();
  // The writer closes stdin once the queue is out; a backend that doesn't exit then is killed.
  // Then both threads stop waiting, even if something the backend started still holds its pipes.
  const os::Status status = os::reap(process->child, grace);
#if defined(_WIN32)
  if (process->watcher.joinable()) {
    process->watcher.join();  // the process is gone, so it has fired
  }
  stop_pipe_io(process->reader, process->reader_io, "stdout reader");
  stop_pipe_io(process->writer, process->writer_io, "stdin writer");
#else
  if (process->stop[1] >= 0) {
    [[maybe_unused]] const auto written = ::write(process->stop[1], "x", 1);
  }
#endif
  process->writer.join();
  process->reader.join();
  os::close_fd(process->child.from_child);
  for (const int fd : {process->pidfd, process->stop[0], process->stop[1]}) {
    if (fd >= 0) {
      os::close_fd(fd);
    }
  }
  return status;
}

namespace {

#if !defined(_WIN32)
/// Waits until `fd` can be read (or written, `out`), the process has exited (`pidfd`), or `stop`
/// is signalled. Returns which: 0 fd, 1 exited, 2 stopped, -1 an error.
int wait_for(int fd, bool out, int pidfd, int stop) {
  struct pollfd fds[3] = {{fd, static_cast<short>(out ? POLLOUT : POLLIN), 0}, {stop, POLLIN, 0}, {pidfd, POLLIN, 0}};
  for (;;) {
    const int n = ::poll(fds, pidfd >= 0 ? 3 : 2, -1);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      return -1;
    }
    if (fds[0].revents != 0) {
      return 0;  // data (or EOF / an error, which the read or write reports)
    }
    if (fds[1].revents != 0) {
      return 2;
    }
    return 1;  // the process exited, and nothing is left to read or room to write
  }
}

#else
/// Windows: the reads and writes block; the exit or ending the process interrupts them (os::interrupt_io).
int wait_for(int, bool, int, int) { return 0; }
#endif

}  // namespace

void Link::read_loop(Process* process) {
  ipc::FrameBuffer frames(kMaxMessage);
  std::vector<std::byte> chunk(64 * 1024);
  const std::uint64_t generation = process->generation;
  // Hands the messages in the `n` bytes read to the UI thread; false once the framing broke.
  const auto deliver = [&](long n) {
    frames.push(std::span<const std::byte>(chunk.data(), static_cast<std::size_t>(n)));
    while (auto message = frames.next()) {
      CefPostTask(TID_UI, base::BindOnce(&Link::on_backend_message, base::Unretained(g_link), generation,
                                         std::move(*message)));
    }
    if (!frames.error().empty()) {
      CefPostTask(TID_UI, base::BindOnce(&Link::log, base::Unretained(g_link),
                                         "pd-host: the backend broke the framing: " + frames.error()));
      return false;
    }
    return true;
  };
  for (;;) {
    // What the backend wrote before exiting is still read: the pipe comes first.
    if (wait_for(process->child.from_child, false, process->pidfd, process->stop[0]) != 0) {
      break;
    }
    if (!process->reader_io.enter()) {
      break;
    }
    const long n = os::read_some(process->child.from_child, chunk.data(), chunk.size());
    process->reader_io.leave();
    if (n <= 0 || !deliver(n)) {
      break;
    }
  }
  // Interrupted (Windows: the process exited or is being ended): what it wrote before is still
  // read, as POSIX's poll reads the pipe first (#17). Bounded, in case something it started keeps
  // writing.
  if (process->reader_io.interrupted() && frames.error().empty()) {
    for (int i = 0; i < 256; ++i) {
      const std::size_t available = os::bytes_available(process->child.from_child);
      if (available == 0) {
        break;
      }
      const long n = os::read_some(process->child.from_child, chunk.data(), std::min(available, chunk.size()));
      if (n <= 0 || !deliver(n)) {
        break;
      }
    }
  }
  CefPostTask(TID_UI, base::BindOnce(&Link::on_backend_exit, base::Unretained(g_link), generation));
}

namespace {

/// Writes all of `bytes` to the non-blocking `fd`; false once the backend is gone or `stop` came.
bool write_out(int fd, std::span<const std::byte> bytes, [[maybe_unused]] int pidfd, [[maybe_unused]] int stop) {
#if defined(_WIN32)
  return ipc::write_all(fd, bytes);  // blocking; ending the process cancels it
#else
  std::size_t done = 0;
  while (done < bytes.size()) {
    const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
    if (n > 0) {
      done += static_cast<std::size_t>(n);
    } else if (n < 0 && errno == EAGAIN) {
      if (wait_for(fd, true, pidfd, stop) != 0) {
        return false;
      }
    } else if (!(n < 0 && errno == EINTR)) {
      return false;
    }
  }
  return true;
#endif
}

}  // namespace

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
      process->queued_bytes -= message.size();
    }
    // After a failed write the backend is gone: the rest is dropped (the reader reports the exit).
    if (!broken && process->writer_io.enter()) {
      broken = !write_out(process->child.to_child, ipc::frame(message), process->pidfd, process->stop[0]);
      process->writer_io.leave();
    } else {
      broken = true;  // or interrupted: the process exited or is being ended
    }
  }
  os::close_fd(process->child.to_child);
}

}  // namespace pico_desktop::host::cef
