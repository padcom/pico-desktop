#include "signals.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <string>
#include <thread>

#include "host.hpp"
#include "pico_desktop/host/console_event.hpp"
#include "pico_desktop/ipc/process.hpp"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_closure_task.h"

namespace pico_desktop::host::cef {

namespace {

void handle(int signo, int count) {
  if (count == 1) {
    log_line("pd-host: got signal " + std::to_string(signo) + ": closing");
    close_app_window();
  } else {
    log_line("pd-host: got signal " + std::to_string(signo) + " again: quitting now");
    CefQuitMessageLoop();
  }
}

#if defined(_WIN32)

static_assert(CTRL_C_EVENT == static_cast<DWORD>(ConsoleEvent::ctrl_c) &&
              CTRL_BREAK_EVENT == static_cast<DWORD>(ConsoleEvent::ctrl_break) &&
              CTRL_CLOSE_EVENT == static_cast<DWORD>(ConsoleEvent::close) &&
              CTRL_LOGOFF_EVENT == static_cast<DWORD>(ConsoleEvent::logoff) &&
              CTRL_SHUTDOWN_EVENT == static_cast<DWORD>(ConsoleEvent::shutdown));

// Set by signal_watch_shutdown_done (manual reset: every waiting handler returns).
HANDLE g_shutdown_done = nullptr;

// Ctrl+C, Ctrl+Break, the console closing, logoff, shutdown: on a thread of the system's.
BOOL WINAPI on_console_event(DWORD type) {
  static std::atomic<int> count{0};
  CefPostTask(TID_UI, base::BindOnce(&handle, static_cast<int>(type), ++count));
  if (ends_process_on_return(type) && g_shutdown_done != nullptr) {
    // Returning now would end the process before `closing` reached the page and the backend:
    // wait for the orderly close (the system's own timeout still bounds it).
    WaitForSingleObject(g_shutdown_done, INFINITE);
  }
  return TRUE;
}

#else

int g_pipe[2] = {-1, -1};

// Async-signal-safe: only a write.
void on_signal(int signo) {
  const auto byte = static_cast<char>(signo);
  [[maybe_unused]] const auto written = ::write(g_pipe[1], &byte, 1);
}

void watch() {
  int count = 0;
  for (;;) {
    char byte = 0;
    const ssize_t n = ::read(g_pipe[0], &byte, 1);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      return;
    }
    CefPostTask(TID_UI, base::BindOnce(&handle, static_cast<int>(byte), ++count));
  }
}

#endif

}  // namespace

#if defined(_WIN32)
void start_signal_watch() {
  g_shutdown_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  SetConsoleCtrlHandler(on_console_event, TRUE);
}

void signal_watch_shutdown_done() {
  if (g_shutdown_done != nullptr) {
    SetEvent(g_shutdown_done);
  }
}
#else
void start_signal_watch() {
  if (!pico_desktop::ipc::cloexec_pipe(g_pipe)) {
    return;
  }
  struct sigaction action {};
  action.sa_handler = on_signal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  for (const int signo : {SIGTERM, SIGINT, SIGHUP}) {
    sigaction(signo, &action, nullptr);
  }
  std::thread(watch).detach();
}

void signal_watch_shutdown_done() {}
#endif

}  // namespace pico_desktop::host::cef
