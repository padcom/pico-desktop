#include "control.hpp"

#include <unistd.h>

#include <cerrno>
#include <string>
#include <thread>

#include "host.hpp"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "link.hpp"
#include "pico_desktop/host/options.hpp"

namespace pico_desktop::host::cef {

namespace {

void quit() {
  CefRefPtr<CefBrowser> browser = browser_state().browser;
  if (browser) {
    browser->GetHost()->CloseBrowser(false);
  } else {
    CefQuitMessageLoop();
  }
}

void run(ControlCommand command) {
  CEF_REQUIRE_UI_THREAD();
  switch (command) {
    case ControlCommand::restart_backend:
      link().restart_backend();
      break;
    case ControlCommand::reload:
      if (CefRefPtr<CefBrowser> browser = browser_state().browser) {
        log_line("pd-host: reloading the page (requested)");
        browser->ReloadIgnoreCache();
      }
      break;
    case ControlCommand::quit:
      log_line("pd-host: quitting (requested)");
      quit();
      break;
  }
}

void unknown(std::string line) { log_line("pd-host: unknown control command '" + line + "'"); }

void read_commands() {
  std::string pending;
  char chunk[256];
  for (;;) {
    const ssize_t n = ::read(STDIN_FILENO, chunk, sizeof chunk);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      break;
    }
    pending.append(chunk, static_cast<std::size_t>(n));
    for (std::size_t end = pending.find('\n'); end != std::string::npos; end = pending.find('\n')) {
      std::string line = pending.substr(0, end);
      pending.erase(0, end + 1);
      if (const auto command = parse_control(line)) {
        CefPostTask(TID_UI, base::BindOnce(&run, *command));
      } else if (line.find_first_not_of(" \t\r") != std::string::npos) {
        CefPostTask(TID_UI, base::BindOnce(&unknown, line));
      }
    }
  }
  // The CLI is gone (or closed our stdin): don't outlive it.
  CefPostTask(TID_UI, base::BindOnce(&run, ControlCommand::quit));
}

}  // namespace

void start_control_reader() {
  // Detached: it blocks in read() until stdin closes, and the process may exit before that.
  std::thread(read_commands).detach();
}

}  // namespace pico_desktop::host::cef
