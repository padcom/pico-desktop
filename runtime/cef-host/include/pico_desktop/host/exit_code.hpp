// How a Windows process's exit code reads in the host's log. Windows has no signals: a crash is
// an NTSTATUS exit code (0xC0000005 is an access violation) and a killed process exits with the
// code TerminateProcess was given.
#pragma once

#include <cstdint>
#include <string>

namespace pico_desktop::host {

/// What the host passes to TerminateProcess when it kills the backend: no program exits with it by
/// itself, so the log can say "killed" (like POSIX's "signal 9").
inline constexpr std::uint32_t windows_killed_exit_code = 0xDEAD0009;

/// "killed", "exit code 0xC0000005" for NTSTATUS values (the top bit set: errors and warnings),
/// otherwise "exit code 3".
std::string describe_windows_exit_code(std::uint32_t code);

}  // namespace pico_desktop::host
