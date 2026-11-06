// The host's services for the backend (include/pico_desktop/host/services.hpp lists them and
// the capabilities they need): window controls, file dialogs, notifications, quitting.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "pico_desktop/ipc/value.hpp"

namespace pico_desktop::host::cef {

/// A backend's call to a host service: who to answer (a later backend never gets the answer).
struct ServiceCall {
  std::uint64_t id = 0;
  std::optional<std::uint64_t> window;
  std::uint64_t generation = 0;
};

/// Runs the (known, allowed) service `name`; it answers through link().reply / link().refuse,
/// at once or later (dialogs).
void run_service(const ServiceCall& call, const std::string& name, const ipc::Value& params);

}  // namespace pico_desktop::host::cef
