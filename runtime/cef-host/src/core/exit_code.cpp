#include "pico_desktop/host/exit_code.hpp"

#include <cstdio>

namespace pico_desktop::host {

std::string describe_windows_exit_code(std::uint32_t code) {
  if (code == windows_killed_exit_code) {
    return "killed";
  }
  if ((code & 0x80000000u) != 0) {
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%08X", static_cast<unsigned>(code));
    return std::string("exit code ") + hex;
  }
  return "exit code " + std::to_string(code);
}

}  // namespace pico_desktop::host
