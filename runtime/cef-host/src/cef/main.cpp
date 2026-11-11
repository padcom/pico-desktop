// pico-desktop-host: the CEF Views host (docs/architecture.md). The same executable runs as the
// browser process and as every Chromium subprocess.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "app.hpp"
#include "clipboard.hpp"
#include "control.hpp"
#include "signals.hpp"
#include "host.hpp"
#include "link.hpp"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_version.h"
#include "pico_desktop/host/sandbox.hpp"
#include "pico_desktop/host/temp_dir.hpp"

namespace pico_desktop::host::cef {

namespace {

BrowserState* g_state = nullptr;  // browser process only; lives until exit
void* g_sandbox_info = nullptr;   // Windows: from the bootstrap (main_win.cpp)

int process_id() {
#if defined(_WIN32)
  return ::_getpid();
#else
  return static_cast<int>(::getpid());
#endif
}

namespace fs = std::filesystem;

/// Where the host's profiles live: %LOCALAPPDATA% on Windows, the XDG cache elsewhere.
fs::path cache_base() {
#if defined(_WIN32)
  const char* local = std::getenv("LOCALAPPDATA");
  return local && *local ? fs::path(local) : fs::temp_directory_path();
#else
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  const char* home = std::getenv("HOME");
  return xdg && *xdg ? fs::path(xdg) : fs::path(home ? home : "/tmp") / ".cache";
#endif
}

fs::path cache_root(const Options& options, bool& temporary) {
  if (options.mode == Mode::self_test) {
    // Self-tests never touch (or lock) a real profile.
    temporary = true;
#if defined(_WIN32)
    // Windows: a process can't remove its own profile (#23), so self-test profiles are named by
    // pid and swept like --multi-instance ones: each run removes ended runs' (#41). %LOCALAPPDATA%
    // is per user, so the name needn't be random.
    const fs::path runs = cache_base() / "pico-desktop" / "self-test";
    remove_ended_instances(runs, process_id(), process_alive);
    const fs::path dir = runs / std::to_string(process_id());
    std::error_code error;
    remove_temp_dir(dir, std::chrono::milliseconds(0), error);  // an ended process's with this pid
    fs::create_directories(dir, error);
    return error ? fs::path() : dir;
#else
    // POSIX: a private (0700) directory with a random name in the shared /tmp, removed at exit.
    return make_private_temp_dir("pd-host-self-test-");
#endif
  }
  const fs::path app = cache_base() / "pico-desktop" / "apps" / options.policy.app_id;
  if (options.multi_instance) {
    // Each instance its own profile (CEF allows one process per profile), removed on exit, or
    // where that can't be done (Windows, #23) by the next --multi-instance run.
    temporary = true;
    remove_ended_instances(app / "instances", process_id(), process_alive);
    return app / "instances" / std::to_string(process_id());
  }
  return app;
}

}  // namespace

BrowserState& browser_state() { return *g_state; }

void set_sandbox_info(void* info) { g_sandbox_info = info; }

void init_browser_state(Options options) {
  g_state = new BrowserState{};
  g_state->options = std::move(options);
}

void log_line(const std::string& line) {
  std::fputs(line.c_str(), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

int run_browser(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  ParseResult parsed = parse_options(args);
  if (const auto* error = std::get_if<std::string>(&parsed)) {
    std::fprintf(stderr, "pico-desktop-host: %s\n", error->c_str());
    return 2;
  }
  Options options = std::get<Options>(std::move(parsed));
  if (options.mode == Mode::version) {
    std::printf("pico-desktop-host %s (CEF %s)\n", PD_VERSION, CEF_VERSION);
    return 0;
  }
  if (options.mode == Mode::help) {
    std::fputs(usage().c_str(), stdout);
    return 0;
  }
  if (!options.policy.dev_origin) {
    std::error_code error;
    options.app_dir = fs::canonical(options.app_dir, error);
    if (error || !fs::is_regular_file(options.app_dir / "index.html")) {
      std::fprintf(stderr, "pico-desktop-host: --app-dir must be a built frontend with an index.html\n");
      return 2;
    }
  }
  bool temporary_cache = false;
  const fs::path cache = cache_root(options, temporary_cache);
  if (cache.empty()) {
    std::fprintf(stderr, "pico-desktop-host: couldn't create a cache directory\n");
    return 1;
  }
#if defined(__linux__)
  // Without user namespaces Chromium falls back to its setuid helper, and without that it aborts
  // with a trap; say what's wrong instead (the host never runs unsandboxed).
  std::error_code exe_error;
  const fs::path exe = fs::read_symlink("/proc/self/exe", exe_error);
  const std::string helper = ((exe_error ? fs::path(argv[0]) : exe).parent_path() / "chrome-sandbox").string();
  if (!user_namespaces_available() && !suid_sandbox_usable(helper)) {
    std::fprintf(stderr, "pico-desktop-host: %s\n", sandbox_unavailable_message(helper).c_str());
    return 1;
  }
#endif
  std::error_code error;
  fs::create_directories(cache, error);
  init_browser_state(std::move(options));
  init_link(browser_state().options);

#if defined(_WIN32)
  CefMainArgs main_args(::GetModuleHandleW(nullptr));
#else
  CefMainArgs main_args(argc, argv);
#endif
  CefRefPtr<HostApp> app = new HostApp();
  CefSettings settings;
  settings.no_sandbox = false;  // never disabled (PROJECT_DESCRIPTION.md §6)
  settings.remote_debugging_port = 0;
  settings.persist_session_cookies = false;
  CefString(&settings.root_cache_path) = cache.string();
  CefString(&settings.cache_path) = cache.string();
  const bool self_test = browser_state().options.mode == Mode::self_test;
  if (temporary_cache && !self_test) {
    // No log file for a --multi-instance profile (FATAL messages still go to stderr): Chromium
    // keeps the log open until the process exits, without FILE_SHARE_DELETE, so on Windows a
    // sibling file couldn't be removed and each launch would leave one; inside the profile it
    // goes with it, unread. A self-test does log there: its profile is kept when it fails, for
    // the log (on Windows the next self-test sweeps it, #41).
    settings.log_severity = LOGSEVERITY_DISABLE;
  } else {
    settings.log_severity = LOGSEVERITY_WARNING;
    CefString(&settings.log_file) = (cache / "cef.log").string();
  }
  if (!CefInitialize(main_args, settings, app, g_sandbox_info)) {
    if (CefGetExitCode() == CEF_RESULT_CODE_NORMAL_EXIT_PROCESS_NOTIFIED) {
      std::printf("pico-desktop-host: %s is already running; its window was brought to the front\n",
                  browser_state().options.policy.app_id.c_str());
      return 0;
    }
    std::fprintf(stderr,
                 "pico-desktop-host: CEF failed to initialize (exit code %d). If the sandbox can't start, "
                 "enable unprivileged user namespaces; the host never runs without the sandbox.\n",
                 CefGetExitCode());
    return 1;
  }
  start_signal_watch();
  if (browser_state().options.control_stdin) {
    start_control_reader();
  }
  CefRunMessageLoop();
  // The backend got `closing` when the last window closed; give it a moment to finish.
  link().stop(std::chrono::milliseconds(2000));
  pico_desktop::host::cef::clipboard::shutdown();  // before CEF goes: hands the clipboard's text to a clipboard manager
  CefShutdown();

  const BrowserState& state = browser_state();
  int code = 0;
  if (self_test) {
    code = state.self_test_done && state.self_test_failed == 0 && state.self_test_passed > 0 ? 0 : 1;
  }
  // A failed self-test keeps its profile, and with it CEF's log: often the only clue on CI. (The
  // profile path is never empty here: run_browser stops early when it couldn't make one.)
  const bool keep = self_test && code != 0 && !cache.empty();
  if (keep) {
    std::fprintf(stderr, "pico-desktop-host: the self-test failed; its CEF log is kept: %s\n",
                 (cache / "cef.log").string().c_str());
  }
  // A temporary profile (self-test, --multi-instance) goes with the instance. On Windows that
  // can't fully work: this process holds Chromium's files in it until it ends (#23). So there it's
  // one pass, and what's left goes with the next --multi-instance run's sweep
  // (remove_ended_instances), not a failure. Elsewhere a held file is only a subprocess finishing:
  // retried, then reported.
#if defined(_WIN32)
  if (temporary_cache && !keep) {
    remove_temp_dir(cache, std::chrono::milliseconds(0), error);
  }
#else
  if (temporary_cache && !keep && !remove_temp_dir(cache, std::chrono::seconds(10), error)) {
    std::fprintf(stderr, "pico-desktop-host: couldn't remove the temporary profile %s: %s\n", cache.string().c_str(),
                 error.message().c_str());
    // What's left, so the log says which files were held.
    std::error_code walk;
    int listed = 0;
    for (auto it = fs::recursive_directory_iterator(cache, walk); !walk && it != fs::recursive_directory_iterator() && listed < 20;
         it.increment(walk), ++listed) {
      std::fprintf(stderr, "pico-desktop-host:   left: %s\n", it->path().string().c_str());
    }
  }
#endif
  signal_watch_shutdown_done();  // a console close/logoff/shutdown handler may be waiting for this
  return code;
}

}  // namespace pico_desktop::host::cef

#if !defined(__APPLE__) && !defined(_WIN32)  // macOS: main_mac.mm and helper_mac.cpp; Windows: main_win.cpp
int main(int argc, char* argv[]) {
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromArgv(argc, argv);
  if (command_line->HasSwitch("type")) {
    // A Chromium subprocess (renderer, GPU, utility, zygote).
    CefMainArgs main_args(argc, argv);
    CefRefPtr<pico_desktop::host::cef::HostApp> app = new pico_desktop::host::cef::HostApp();
    return CefExecuteProcess(main_args, app, nullptr);
  }
  return pico_desktop::host::cef::run_browser(argc, argv);
}
#endif
