// The host's command line. Arguments it doesn't know are left to Chromium/CEF (which reads its
// own `--switches` from the same command line).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "pico_desktop/host/policy.hpp"

namespace pico_desktop::host {

enum class Mode {
  /// Show the app.
  run,
  /// Load the app, let its page test itself (`pd-test:` console lines), check that the renderer
  /// is sandboxed, and exit with the result.
  self_test,
  version,
  help,
};

struct Options {
  Mode mode = Mode::run;
  std::filesystem::path app_dir;
  Policy policy;
  /// Development policy: DevTools (F12), the dev-server origin, remote debugging allowed.
  bool dev = false;
  /// Hand external links to the system browser (off in tests: they're only logged).
  bool open_external = true;
  /// A remote-debugging switch was given (refused outside development).
  bool remote_debugging_requested = false;
};

/// `Options`, or an error message for the user.
using ParseResult = std::variant<Options, std::string>;

/// Parses the host's own arguments (`args` without the program name).
ParseResult parse_options(const std::vector<std::string>& args);

/// The `--help` text.
std::string usage();

}  // namespace pico_desktop::host
