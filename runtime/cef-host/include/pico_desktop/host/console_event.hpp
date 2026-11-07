// Windows console control events (SetConsoleCtrlHandler): which ones end the process as soon as
// the handler returns, so the handler must wait for the orderly close instead (#18).
#pragma once

namespace pico_desktop::host {

/// The event numbers (wincon.h's CTRL_*_EVENT), so this compiles and is tested everywhere.
enum class ConsoleEvent : unsigned long {
  ctrl_c = 0,
  ctrl_break = 1,
  close = 2,
  logoff = 5,
  shutdown = 6,
};

/// True for the console closing, logoff and shutdown: Windows terminates the process once the
/// handler returns (or the system's timeout is up), so returning first would skip `closing`.
/// Ctrl+C and Ctrl+Break don't: the handler returns and the close goes on as usual.
constexpr bool ends_process_on_return(unsigned long type) {
  return type == static_cast<unsigned long>(ConsoleEvent::close) ||
         type == static_cast<unsigned long>(ConsoleEvent::logoff) ||
         type == static_cast<unsigned long>(ConsoleEvent::shutdown);
}

}  // namespace pico_desktop::host
