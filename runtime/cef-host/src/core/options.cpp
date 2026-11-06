#include "pico_desktop/host/options.hpp"

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

}  // namespace pico_desktop::host
