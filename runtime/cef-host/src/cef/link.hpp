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

#include "include/cef_browser.h"
#include "pico_desktop/host/options.hpp"
#include "pico_desktop/host/restart.hpp"
#include "pico_desktop/host/router.hpp"
#include "pico_desktop/ipc/process.hpp"

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
  /// Ends the backend: lets the writer drain, closes its stdin, waits up to `grace`, then kills it.
  void stop(std::chrono::milliseconds grace);

  // RouterOutput
  void to_backend(Bytes message) override;
  void to_session(std::uint64_t session, Bytes message) override;
  void log(const std::string& line) override;

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
    bool draining = false;  // set when the writer should finish the queue and close stdin
  };

  void spawn();
  void on_backend_message(std::uint64_t generation, Bytes message);
  void on_backend_exit(std::uint64_t generation);
  /// Starts the backend again after a backoff, or announces `failed` once the budget is spent.
  void schedule_restart();
  /// Ends the current process: closes its stdin, reaps it (killing it after `grace`), joins its
  /// threads. Returns the wait status (-1 if unknown).
  int end_process(std::chrono::milliseconds grace);
  static void read_loop(Process* process);
  static void write_loop(Process* process);

  Options options_;
  Router router_;
  RestartBudget budget_;
  bool closing_ = false;
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
