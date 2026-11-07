// The browser process's side of the protocol: page sessions (one per bridge context) and the
// supervised Pico backend, joined by the Router. The backend's pipes are served by two threads
// per process (a reader that posts messages to the UI thread, a writer that drains a queue), so
// the UI thread never blocks on the backend. Everything else runs on the UI thread.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "pico_desktop/host/options.hpp"
#include "pico_desktop/host/restart.hpp"
#include "pico_desktop/host/router.hpp"
#include "pico_desktop/ipc/process.hpp"
#include "services.hpp"

namespace pico_desktop::host::cef {

class Link : public RouterOutput {
 public:
  explicit Link(const Options& options);
  ~Link() override;

  /// Starts the backend, if the app has one (UI thread, once CEF is up).
  void start();
  /// A message from the bridge context `context` of `frame`.
  void from_page(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int context,
                 std::span<const std::byte> message);
  /// That bridge context was released (navigation, reload, frame gone).
  void page_closed(const std::string& frame_id, int context);
  /// Every page of `browser` is gone (its renderer ended, or the browser closed).
  void browser_gone(int browser_id);
  /// The app is closing: tell the pages and the backend (the backend gets a grace period in `stop`).
  void closing();
  /// An orderly close (the window was asked to close): the pages and the backend get `closing`
  /// while the window is still there; `done` runs once the backend has exited, or was killed
  /// after the grace period. Never blocks the UI thread.
  void begin_close(base::OnceClosure done);
  /// Development (`restart-backend`): the backend gets `closing`, and once it's gone (or killed
  /// after a grace period) the program starts again, a rebuilt one included; the pages stay. It
  /// doesn't use the restart budget, and it starts a backend that had `failed`.
  void restart_backend();
  /// Ends the backend: lets the writer drain, closes its stdin, waits up to `grace`, then kills it.
  void stop(std::chrono::milliseconds grace);

  // RouterOutput
  void to_backend(Bytes message) override;
  void to_session(std::uint64_t session, Bytes message) override;
  void log(const std::string& line) override;
  void kill_backend() override;
  void pong() override;
  bool backend_busy() const override;
  void host_request(std::uint64_t id, const std::string& name, std::optional<Bytes> payload,
                    std::optional<std::uint64_t> window) override;

  /// Answers a host-service call (dropped if that backend is gone).
  void reply(const ServiceCall& call, std::optional<Bytes> payload);
  /// Fails a host-service call (dropped if that backend is gone).
  void refuse(const ServiceCall& call, const char* code, const std::string& message);

 private:
  struct Page {
    int browser_id;
    std::string frame_id;
    int context;
  };

  /// One backend process and its two threads.
  struct Process {
    ipc::ChildProcess child;
    std::uint64_t generation = 0;
    std::thread reader;
    std::thread writer;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Bytes> queue;
    std::size_t queued_bytes = 0;
    bool draining = false;  // set when the writer should finish the queue and close stdin
    // Readable once the process has exited, even while something it started still holds its
    // pipes (Linux pidfd; -1 where there's none: then only EOF tells).
    int pidfd = -1;
    // Written by end_process: the reader and writer stop waiting on the pipes.
    int stop[2] = {-1, -1};
    // Liveness: pings sent, and when the last pong came (judged only once one came).
    std::uint64_t pings = 0;
    bool answered = false;
    std::chrono::steady_clock::time_point last_pong;
  };

  /// Logs a message's envelope with --trace-ipc.
  void trace(const char* direction, std::span<const std::byte> message);
  void spawn();
  void on_backend_message(std::uint64_t generation, Bytes message);
  void on_backend_exit(std::uint64_t generation);
  /// Starts the backend again after a backoff, or announces `failed` once the budget is spent.
  void schedule_restart();
  /// Kills process `generation` if it's still running (a planned restart's grace period is over).
  void force_end(std::uint64_t generation);
  void finish_close();
  /// Pings the backend of `generation` and judges its answers, every liveness interval.
  void liveness_tick(std::uint64_t generation);
  /// Ends the current process: closes its stdin, reaps it (killing it after `grace`), joins its
  /// threads. Returns the wait status (-1 if unknown).
  int end_process(std::chrono::milliseconds grace);
  static void read_loop(Process* process);
  static void write_loop(Process* process);

  Options options_;
  Router router_;
  RestartBudget budget_;
  bool closing_ = false;
  bool planned_restart_ = false;
  base::OnceClosure close_done_;
  std::uint64_t generation_ = 0;
  std::unique_ptr<Process> process_;
  std::map<std::string, std::uint64_t> session_of_;  // "<frame id>#<context>" → session
  std::map<std::uint64_t, Page> pages_;
  std::uint64_t next_session_ = 1;
};

/// The browser process's link (valid after `init_link`, until exit).
Link& link();
void init_link(const Options& options);

}  // namespace pico_desktop::host::cef
