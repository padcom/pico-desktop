#include "signals.hpp"

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <thread>

#include "host.hpp"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_closure_task.h"

namespace pico_desktop::host::cef {

namespace {

int g_pipe[2] = {-1, -1};

// Async-signal-safe: only a write.
void on_signal(int signo) {
  const auto byte = static_cast<char>(signo);
  [[maybe_unused]] const auto written = ::write(g_pipe[1], &byte, 1);
}

void handle(int signo, int count) {
  if (count == 1) {
    log_line("pd-host: got signal " + std::to_string(signo) + ": closing");
    close_app_window();
  } else {
    log_line("pd-host: got signal " + std::to_string(signo) + " again: quitting now");
    CefQuitMessageLoop();
  }
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

}  // namespace

void start_signal_watch() {
  if (::pipe2(g_pipe, O_CLOEXEC) != 0) {
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

}  // namespace pico_desktop::host::cef
