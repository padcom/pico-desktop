// pico-desktop-host: the CEF Views host (docs/architecture.md). The same executable runs as the
// browser process and as every Chromium subprocess.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
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
#include "control.hpp"
#include "signals.hpp"
#include "host.hpp"
#include "link.hpp"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_version.h"
#include "pico_desktop/host/sandbox.hpp"

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

fs::path cache_root(const Options& options, bool& temporary) {
  if (options.mode == Mode::self_test) {
    // Self-tests never touch (or lock) a real profile.
    temporary = true;
    for (int attempt = 0; attempt < 100; ++attempt) {
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      const fs::path dir = fs::temp_directory_path() /
                           ("pd-host-self-test-" + std::to_string(process_id()) + "-" + std::to_string(stamp + attempt));
      std::error_code error;
      if (fs::create_directory(dir, error)) {
        return dir;
      }
    }
    return {};
  }
#if defined(_WIN32)
  const char* local = std::getenv("LOCALAPPDATA");
  const fs::path base = local && *local ? fs::path(local) : fs::temp_directory_path();
#else
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  const char* home = std::getenv("HOME");
  const fs::path base = xdg && *xdg ? fs::path(xdg) : fs::path(home ? home : "/tmp") / ".cache";
#endif
  const fs::path app = base / "pico-desktop" / "apps" / options.policy.app_id;
  if (options.multi_instance) {
    // Each instance its own profile (CEF allows one process per profile), removed on exit.
    temporary = true;
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
  CefRefPtr<HostApp> app = new HostApp(true);
  CefSettings settings;
  settings.no_sandbox = false;  // never disabled (PROJECT_DESCRIPTION.md §6)
  settings.remote_debugging_port = 0;
  settings.persist_session_cookies = false;
  settings.log_severity = LOGSEVERITY_WARNING;
  CefString(&settings.root_cache_path) = cache.string();
  CefString(&settings.cache_path) = cache.string();
  CefString(&settings.log_file) = (cache / "cef.log").string();
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
  CefShutdown();

  const BrowserState& state = browser_state();
  if (temporary_cache) {
    fs::remove_all(cache, error);
  }
  if (state.options.mode == Mode::self_test) {
    return state.self_test_done && state.self_test_failed == 0 && state.self_test_passed > 0 ? 0 : 1;
  }
  return 0;
}

}  // namespace pico_desktop::host::cef

#if !defined(__APPLE__) && !defined(_WIN32)  // macOS: main_mac.mm and helper_mac.cpp; Windows: main_win.cpp
int main(int argc, char* argv[]) {
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromArgv(argc, argv);
  if (command_line->HasSwitch("type")) {
    // A Chromium subprocess (renderer, GPU, utility, zygote).
    CefMainArgs main_args(argc, argv);
    CefRefPtr<pico_desktop::host::cef::HostApp> app = new pico_desktop::host::cef::HostApp(false);
    return CefExecuteProcess(main_args, app, nullptr);
  }
  return pico_desktop::host::cef::run_browser(argc, argv);
}
#endif
