// The system clipboard for the backend's host.clipboard.* services. CEF has no clipboard API, so
// on Linux this is an X11 client of its own (XCB, on its own connection and thread, so nothing is
// shared with Chromium's): it owns the CLIPBOARD selection while it holds text, answers other
// programs' requests for it, and converts the selection when it reads. Under Wayland that's
// XWayland, which keeps the two clipboards in sync.
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace pico_desktop::host::cef::clipboard {

/// The most text either way (the protocol's messages are bounded; the clipboard isn't).
constexpr std::size_t kMaxText = std::size_t{1} << 20;

/// How it went: no error is success.
struct Result {
  /// The text read (none: the clipboard has no text).
  std::optional<std::string> text;
  /// Empty on success, else a protocol error code (UNAVAILABLE, TOO_LARGE, TIMEOUT) …
  std::string code;
  /// … and what happened.
  std::string message;
};

using Done = std::function<void(Result)>;

/// Puts `text` on the clipboard. `done` runs on the clipboard's thread.
void write_text(std::string text, Done done);
/// Reads the clipboard's text. `done` runs on the clipboard's thread.
void read_text(Done done);
/// At exit: hands what this app put on the clipboard to the clipboard manager, if there is one
/// (otherwise X11 text goes away with its owner), and ends the thread.
void shutdown();

}  // namespace pico_desktop::host::cef::clipboard
