// The host's command line. Arguments it doesn't know are left to Chromium/CEF (which reads its
// own `--switches` from the same command line).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
  /// The compiled Pico backend; none: the page has no backend (requests fail with DISCONNECTED).
  std::filesystem::path backend;
  Policy policy;
  /// Development policy: DevTools (F12), the dev-server origin, remote debugging allowed.
  bool dev = false;
  /// Hand external links to the system browser (off in tests: they're only logged).
  bool open_external = true;
  /// A remote-debugging switch was given (refused outside development).
  bool remote_debugging_requested = false;
  /// Development: read control commands from stdin, one per line (the dev CLI drives the host).
  bool control_stdin = false;
  /// Allow several instances (each with its own, temporary profile). By default a second launch
  /// brings the running instance's window to the front and exits.
  bool multi_instance = false;
  /// Development: log every message's envelope (kind, id, name, window, subscription, size), never
  /// its payload.
  bool trace_ipc = false;
};

/// What the dev CLI can ask of the host (`--control=stdin`).
enum class ControlCommand {
  /// Stop the backend (it gets `closing`) and start it again: a rebuilt backend, same window.
  restart_backend,
  /// Reload the page.
  reload,
  /// Close the window and exit.
  quit,
};

/// The command on one control line (surrounding whitespace ignored), if it is one.
std::optional<ControlCommand> parse_control(std::string_view line);

/// `Options`, or an error message for the user.
using ParseResult = std::variant<Options, std::string>;

/// Parses the host's own arguments (`args` without the program name).
ParseResult parse_options(const std::vector<std::string>& args);

/// The `--help` text.
std::string usage();

}  // namespace pico_desktop::host
