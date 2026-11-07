// Handing external links to the system browser.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#endif

#include <string>
#include <thread>

#include "host.hpp"

#if !defined(_WIN32)
extern char** environ;
#endif

namespace pico_desktop::host::cef {

void open_external(const std::string& url) {
  if (!browser_state().options.open_external) {
    log_line("pd-host: external open " + url + " (logged only: --no-external-open)");
    return;
  }
  log_line("pd-host: external open " + url);
#if defined(__linux__) || defined(__APPLE__)
  // xdg-open (macOS: open), without a shell (the URL is an argument, never parsed as a command).
#if defined(__APPLE__)
  char program[] = "open";
#else
  char program[] = "xdg-open";
#endif
  std::string arg = url;
  char* argv[] = {program, arg.data(), nullptr};
  pid_t pid = 0;
  if (posix_spawnp(&pid, program, nullptr, nullptr, argv, environ) != 0) {
    log_line(std::string("pd-host: couldn't start ") + program + " for " + url);
    return;
  }
  // Reap it off the UI thread (it exits once it has handed the URL over).
  std::thread([pid] {
    int status = 0;
    waitpid(pid, &status, 0);
  }).detach();
#elif defined(_WIN32)
  // The default browser, through the shell (the URL is a parameter, never a command line).
  const int n = MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), wide.data(), n);
  const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (result <= 32) {
    log_line("pd-host: couldn't open " + url + " (ShellExecute " + std::to_string(result) + ")");
  }
#else
#error "open_external is implemented for Linux, macOS and Windows"
#endif
}

}  // namespace pico_desktop::host::cef
