// The host's own services, which the backend calls by name (`host.window.setTitle`, …) as
// ordinary requests. Each group needs a capability the app declares (pico-desktop.config.ts
// `capabilities`, the host's --capabilities); `host.app.*` needs none.
#pragma once

#include <optional>
#include <string_view>

namespace pico_desktop::host {

enum class Capability { window, dialogs, notifications };

/// The capability named `name` (`window`, `dialogs`, `notifications`).
std::optional<Capability> parse_capability(std::string_view name);
const char* capability_name(Capability capability);

struct ServiceInfo {
  bool known = false;
  /// What calling it needs (nothing for `host.app.*`).
  std::optional<Capability> needs;
};

/// What the service `name` is, from the host's table.
ServiceInfo service_info(std::string_view name);

}  // namespace pico_desktop::host
