#include "pico_desktop/host/services.hpp"

#include <array>
#include <utility>

namespace pico_desktop::host {

namespace {

struct Entry {
  std::string_view name;
  std::optional<Capability> needs;
};

constexpr std::array kServices = {
    Entry{"host.window.setTitle", Capability::window},  Entry{"host.window.getTitle", Capability::window},
    Entry{"host.window.minimize", Capability::window},  Entry{"host.window.maximize", Capability::window},
    Entry{"host.window.restore", Capability::window},   Entry{"host.window.focus", Capability::window},
    Entry{"host.window.setSize", Capability::window},   Entry{"host.window.getBounds", Capability::window},
    Entry{"host.dialog.openFile", Capability::dialogs}, Entry{"host.dialog.saveFile", Capability::dialogs},
    Entry{"host.dialog.openFolder", Capability::dialogs},
    Entry{"host.notification.show", Capability::notifications},
    // Like the page's: writing needs nothing, reading the `clipboard` capability.
    Entry{"host.clipboard.writeText", std::nullopt},     Entry{"host.clipboard.readText", Capability::clipboard},
    Entry{"host.app.quit", std::nullopt},
};

}  // namespace

std::optional<Capability> parse_capability(std::string_view name) {
  for (const auto c : {Capability::window, Capability::dialogs, Capability::notifications, Capability::clipboard}) {
    if (name == capability_name(c)) {
      return c;
    }
  }
  return std::nullopt;
}

const char* capability_name(Capability capability) {
  switch (capability) {
    case Capability::window:
      return "window";
    case Capability::dialogs:
      return "dialogs";
    case Capability::notifications:
      return "notifications";
    case Capability::clipboard:
      return "clipboard";
  }
  return "?";
}

ServiceInfo service_info(std::string_view name) {
  for (const Entry& e : kServices) {
    if (e.name == name) {
      return {true, e.needs};
    }
  }
  return {};
}

}  // namespace pico_desktop::host
