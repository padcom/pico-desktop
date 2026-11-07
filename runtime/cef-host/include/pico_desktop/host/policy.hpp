// The host's security decisions (docs/architecture.md, SecurityPolicy): which URLs are the app's
// trusted origin, what happens on navigation, and which frames get the native bridge.
// No CEF types, so it's unit-tested alone.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pico_desktop::host {

/// How the trusted origin is spelled (ADR-0003: https, the default, after comparing the two).
enum class OriginScheme {
  /// `https://<app-id>.localhost/`: the built-in https scheme, intercepted by the host. Cookies,
  /// the Cache API and Service Workers work.
  https,
  /// `app://<app-id>/`: a registered standard, secure custom scheme (no cookies without extra
  /// configuration, no Cache API, no Service Workers).
  app,
};

struct Policy {
  std::string app_id;
  OriginScheme scheme = OriginScheme::https;
  /// The Vite dev server's origin (`http://localhost:5173`) under the development policy only.
  std::optional<std::string> dev_origin;
  /// Whether same-origin subframes get the bridge too (off: only the main frame).
  bool bridge_in_subframes = false;
};

/// The trusted origin, without a trailing slash: `https://<id>.localhost` or `app://<id>`.
std::string trusted_origin(const Policy& policy);

/// The host part of the trusted origin (`<id>` or `<id>.localhost`).
std::string trusted_host(const Policy& policy);

/// The URL the window opens with.
std::string start_url(const Policy& policy);

/// Whether `url` is on the trusted origin (or the dev origin, when there is one).
bool is_trusted_url(const Policy& policy, std::string_view url);

enum class Navigation {
  /// Load it in the app window.
  allow,
  /// Don't load it; hand it to the system browser.
  open_external,
  /// Don't load it at all.
  block,
};

/// What to do with a navigation (or a popup) to `url`: trusted URLs load, web and mail links go
/// to the system browser, everything else (`file:`, `data:`, `javascript:`, `chrome:`, …) is
/// blocked.
Navigation decide_navigation(const Policy& policy, std::string_view url);

/// Whether a frame showing `frame_url` gets the native bridge.
bool bridge_allowed(const Policy& policy, std::string_view frame_url, bool is_main_frame);

/// Whether a page's permission prompt is granted: only reading the clipboard (`clipboard_only`),
/// only for the trusted origin, and only when the app declared the `clipboard` capability.
/// Everything else (camera, location, notifications, …) is denied: a desktop app has no
/// prompt UI, and an unanswered prompt would leave the page's promise pending forever.
bool permission_granted(const Policy& policy, std::string_view origin, bool clipboard_only, bool clipboard_declared);

/// Whether `id` can be an app ID: a DNS label sequence (`hello`, `com.example.hello`), lowercase.
bool is_valid_app_id(std::string_view id);

}  // namespace pico_desktop::host
