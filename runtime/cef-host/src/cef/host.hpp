// Shared declarations of the CEF host. CEF targets build with -fno-exceptions -fno-rtti
// -fno-threadsafe-statics: no throwing, no function-local statics shared across threads.
#pragma once

#include <string>

#include "include/cef_browser.h"
#include "pico_desktop/host/options.hpp"

namespace pico_desktop::host::cef {

// Process messages between a renderer's bridge and the browser process. Each carries the
// sending V8 context's ID first, so a reply can't reach a page that replaced the sender.
inline constexpr const char* kBridgeSend = "pd.send";    // renderer → browser: [context id, text]
inline constexpr const char* kBridgeReply = "pd.reply";  // browser → renderer: [context id, text]

// Switches the browser process adds to its children's command lines (renderers don't see the
// host's options).
inline constexpr const char* kSwitchAppId = "pd-app-id";
inline constexpr const char* kSwitchOrigin = "pd-origin";
inline constexpr const char* kSwitchDevOrigin = "pd-dev-origin";
inline constexpr const char* kSwitchBridgeSubframes = "pd-bridge-subframes";

/// The browser process's state: the options, and the self-test outcome. UI thread only.
struct BrowserState {
  Options options;
  int self_test_passed = 0;
  int self_test_failed = 0;
  bool self_test_done = false;
  int open_browsers = 0;
  /// The app browser while it exists.
  CefRefPtr<CefBrowser> browser;
};

/// The browser process's state (only valid in the browser process, after `init_browser_state`).
BrowserState& browser_state();
void init_browser_state(Options options);

/// Writes `line` to stdout immediately (tests read the host's log lines as they happen).
void log_line(const std::string& line);

/// Hands `url` to the system browser, or only logs it with --no-external-open.
void open_external(const std::string& url);

/// Opens the app window (UI thread, after CEF is initialized).
void create_app_window();

/// Fails the self-test with `reason` and closes the window.
void fail_self_test(CefRefPtr<CefBrowser> browser, const std::string& reason);

}  // namespace pico_desktop::host::cef
