#include "pico_desktop/host/options.hpp"

#include <algorithm>
#include <string_view>

namespace pico_desktop::host {

namespace {

/// The value of `--name=value` when `arg` is that option.
std::optional<std::string> value_of(std::string_view arg, std::string_view name) {
  if (arg.starts_with(name) && arg.size() > name.size() && arg[name.size()] == '=') {
    return std::string(arg.substr(name.size() + 1));
  }
  return std::nullopt;
}

std::optional<std::string> parse_capabilities(const std::string& list, Options& options) {
  std::size_t start = 0;
  while (start <= list.size()) {
    const auto end = std::min(list.find(',', start), list.size());
    const std::string name = list.substr(start, end - start);
    if (!name.empty()) {
      const auto capability = parse_capability(name);
      if (!capability) {
        return "unknown capability '" + name + "': use window, dialogs, notifications";
      }
      options.capabilities.insert(*capability);
    }
    start = end + 1;
  }
  return std::nullopt;
}

/// A non-negative number of at most 7 digits, or -1.
int small_number(const std::string& s) {
  if (s.empty() || s.size() > 7 || s.find_first_not_of("0123456789") != std::string::npos) {
    return -1;
  }
  return std::stoi(s);
}

bool parse_liveness(const std::string& text, Options& options) {
  if (text == "off") {  // a backend stopped in a debugger answers no pings
    options.liveness_interval_ms = 0;
    options.liveness_timeout_ms = 0;
    return true;
  }
  const auto slash = text.find('/');
  if (slash == std::string::npos) {
    return false;
  }
  const int interval = small_number(text.substr(0, slash));
  const int timeout = small_number(text.substr(slash + 1));
  if (interval < 100 || timeout <= interval) {
    return false;
  }
  options.liveness_interval_ms = interval;
  options.liveness_timeout_ms = timeout;
  return true;
}

}  // namespace

std::string usage() {
  return "Usage: pico-desktop-host --app-dir=<dir> [options]\n"
         "\n"
         "Options:\n"
         "  --app-dir=<dir>        the built frontend (index.html and its assets)\n"
         "  --app-id=<id>          the app's ID, the trusted origin's host (default: app)\n"
         "  --origin=https|app     https://<id>.localhost/ (default) or app://<id>/\n"
         "  --backend=<program>    the compiled Pico backend (started with the protocol on stdin/stdout)\n"
         "  --dev                  development policy: DevTools (F12), --dev-url, remote debugging\n"
         "  --dev-url=<origin>     load the Vite dev server instead of --app-dir (needs --dev)\n"
         "  --control=stdin        read restart-backend, reload, quit from stdin (needs --dev)\n"
         "  --trace-ipc            log each message's envelope, not its payload (needs --dev)\n"
         "  --multi-instance       allow several instances (default: a second launch focuses the first)\n"
         "  --capabilities=<list>  the native services the backend may use: window,dialogs,notifications\n"
         "  --liveness=<ms>/<ms>   ping interval and hang timeout for the backend (default 5000/15000),\n"
         "                         or off (for a backend stopped in a debugger)\n"
         "  --window-size=<w>x<h>  the window's initial size (default 1024x768)\n"
         "  --window-title=<text>  the window's title (default: the page's <title>)\n"
         "  --close-grace=<ms>     how long the backend has to finish when the app closes (default 3000)\n"
         "  --self-test            let the page test itself, check the sandbox, exit with the result\n"
         "  --no-external-open     log external links instead of opening the system browser\n"
         "  -V, --version          print the version\n"
         "  -h, --help             print this help\n";
}

ParseResult parse_options(const std::vector<std::string>& args) {
  Options options;
  options.policy.app_id = "app";
  std::optional<std::string> dev_url;
  for (const std::string& arg : args) {
    if (arg == "--version" || arg == "-V") {
      options.mode = Mode::version;
      return options;
    }
    if (arg == "--help" || arg == "-h") {
      options.mode = Mode::help;
      return options;
    }
    if (auto v = value_of(arg, "--app-dir")) {
      options.app_dir = *v;
    } else if (auto id = value_of(arg, "--app-id")) {
      if (!is_valid_app_id(*id)) {
        return "invalid --app-id '" + *id + "': use lowercase letters, digits, '-' and '.' (like com.example.app)";
      }
      options.policy.app_id = *id;
    } else if (auto o = value_of(arg, "--origin")) {
      if (*o == "app") {
        options.policy.scheme = OriginScheme::app;
      } else if (*o == "https") {
        options.policy.scheme = OriginScheme::https;
      } else {
        return "invalid --origin '" + *o + "': use app or https";
      }
    } else if (auto b = value_of(arg, "--backend")) {
      options.backend = *b;
    } else if (auto u = value_of(arg, "--dev-url")) {
      dev_url = *u;
    } else if (auto c = value_of(arg, "--control")) {
      if (*c != "stdin") {
        return "invalid --control '" + *c + "': only stdin";
      }
      options.control_stdin = true;
    } else if (auto c = value_of(arg, "--capabilities")) {
      if (auto problem = parse_capabilities(*c, options)) {
        return *problem;
      }
    } else if (auto ws = value_of(arg, "--window-size")) {
      const auto x = ws->find('x');
      const int w = x == std::string::npos ? -1 : small_number(ws->substr(0, x));
      const int h = x == std::string::npos ? -1 : small_number(ws->substr(x + 1));
      if (w < 200 || h < 150 || w > 16384 || h > 16384) {
        return "invalid --window-size '" + *ws + "': use <width>x<height>, at least 200x150";
      }
      options.window_width = w;
      options.window_height = h;
    } else if (auto t = value_of(arg, "--window-title")) {
      options.window_title = *t;
    } else if (auto g = value_of(arg, "--close-grace")) {
      const int ms = small_number(*g);
      if (ms < 0 || ms > 60000) {
        return "invalid --close-grace '" + *g + "': milliseconds, at most 60000";
      }
      options.close_grace_ms = ms;
    } else if (auto l = value_of(arg, "--liveness")) {
      if (!parse_liveness(*l, options)) {
        return "invalid --liveness '" + *l + "': use <interval ms>/<timeout ms>, the timeout longer than the interval";
      }
    } else if (arg == "--multi-instance") {
      options.multi_instance = true;
    } else if (arg == "--trace-ipc") {
      options.trace_ipc = true;
    } else if (arg == "--dev") {
      options.dev = true;
    } else if (arg == "--self-test") {
      options.mode = Mode::self_test;
    } else if (arg == "--no-external-open") {
      options.open_external = false;
    } else if (arg.starts_with("--remote-debugging")) {
      options.remote_debugging_requested = true;
    } else if (!arg.starts_with("--")) {
      return "unexpected argument '" + arg + "'\n\n" + usage();
    }
    // Other `--switches` are Chromium's.
  }
  if (options.remote_debugging_requested && !options.dev) {
    return "remote debugging is only available with --dev (the production policy never opens a debugging port)";
  }
  if (options.control_stdin && !options.dev) {
    return "--control needs --dev";
  }
  if (options.trace_ipc && !options.dev) {
    return "--trace-ipc needs --dev";
  }
  if (dev_url) {
    if (!options.dev) {
      return "--dev-url needs --dev";
    }
    if (!dev_url->starts_with("http://localhost:") && !dev_url->starts_with("http://127.0.0.1:")) {
      return "--dev-url must be a local origin (http://localhost:<port>)";
    }
    while (dev_url->ends_with('/')) {
      dev_url->pop_back();
    }
    options.policy.dev_origin = *dev_url;
  }
  if (options.app_dir.empty() && !options.policy.dev_origin) {
    return "missing --app-dir\n\n" + usage();
  }
  return options;
}

std::optional<ControlCommand> parse_control(std::string_view line) {
  const auto first = line.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return std::nullopt;
  }
  line = line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1);
  if (line == "restart-backend") {
    return ControlCommand::restart_backend;
  }
  if (line == "reload") {
    return ControlCommand::reload;
  }
  if (line == "quit") {
    return ControlCommand::quit;
  }
  return std::nullopt;
}

}  // namespace pico_desktop::host
