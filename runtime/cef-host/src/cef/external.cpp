// Handing external links to the system browser.
#include <spawn.h>
#include <sys/wait.h>

#include <string>
#include <thread>

#include "host.hpp"

extern char** environ;

namespace pico_desktop::host::cef {

void open_external(const std::string& url) {
  if (!browser_state().options.open_external) {
    log_line("pd-host: external open " + url + " (logged only: --no-external-open)");
    return;
  }
  log_line("pd-host: external open " + url);
#if defined(__linux__)
  // xdg-open, without a shell (the URL is an argument, never parsed as a command).
  char program[] = "xdg-open";
  std::string arg = url;
  char* argv[] = {program, arg.data(), nullptr};
  pid_t pid = 0;
  if (posix_spawnp(&pid, program, nullptr, nullptr, argv, environ) != 0) {
    log_line("pd-host: couldn't start xdg-open for " + url);
    return;
  }
  // Reap it off the UI thread (xdg-open exits once it has handed the URL over).
  std::thread([pid] {
    int status = 0;
    waitpid(pid, &status, 0);
  }).detach();
#else
#error "open_external is implemented for Linux first"
#endif
}

}  // namespace pico_desktop::host::cef
