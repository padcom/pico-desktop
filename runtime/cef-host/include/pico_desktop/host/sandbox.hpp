// Checking that Chromium's child processes really are sandboxed, from the outside: on Linux a
// sandboxed renderer runs under seccomp-bpf (`Seccomp: 2`) in its own PID namespace (two `NSpid`
// entries). The host never disables the sandbox; this is how tests prove it's on.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pico_desktop::host {

struct ProcessSandbox {
  int pid = 0;
  /// Chromium's `--type=` (renderer, gpu-process, utility, zygote, …).
  std::string type;
  /// 0 none, 1 strict, 2 filter (seccomp-bpf); -1 unknown.
  int seccomp_mode = -1;
  /// Whether the process sees itself under a different PID (its own PID namespace).
  bool own_pid_namespace = false;
};

/// What `/proc/<pid>/status` says about seccomp and PID namespaces.
struct StatusFields {
  int seccomp_mode = -1;
  int nspid_levels = 0;
};
StatusFields parse_proc_status(std::string_view status);

/// The parent PID from `/proc/<pid>/stat` (the comm field may contain spaces and parentheses).
std::optional<int> parse_proc_stat_ppid(std::string_view stat);

/// The `--type=` value from `/proc/<pid>/cmdline`, NUL- or (after Chromium rewrote its process
/// title) space-separated; empty for the browser process.
std::string parse_cmdline_type(std::string_view cmdline);

/// Every descendant of `root_pid` started by Chromium (Linux; empty elsewhere).
std::vector<ProcessSandbox> inspect_descendants(int root_pid);

/// Whether every renderer among `processes` is sandboxed, and there's at least one.
bool renderers_sandboxed(const std::vector<ProcessSandbox>& processes);

/// A Chromium child process on Windows, as the browser process sees it: Chromium's sandbox runs a
/// renderer with a restricted token at Low or Untrusted integrity, inside a job object.
struct WindowsProcessSandbox {
  unsigned long pid = 0;
  std::string type;
  /// The token's mandatory integrity level (SECURITY_MANDATORY_*_RID: 0 untrusted, 0x1000 low,
  /// 0x2000 medium, 0x3000 high); -1 when it couldn't be read.
  long integrity = -1;
  bool in_job = false;
};

/// Below this (SECURITY_MANDATORY_MEDIUM_RID) a process can't write to the user's files.
inline constexpr long windows_medium_integrity = 0x2000;

/// Every descendant of `root_pid` started by Chromium (Windows; empty elsewhere).
std::vector<WindowsProcessSandbox> inspect_descendants_windows(unsigned long root_pid);

/// Whether every renderer among `processes` runs below Medium integrity in a job object, and
/// there's at least one (#11).
bool windows_renderers_sandboxed(const std::vector<WindowsProcessSandbox>& processes);

/// Whether this process may create user namespaces (tried in a short-lived child): Chromium's
/// sandbox needs them, unless a setuid-root `chrome-sandbox` helper is installed (Linux).
bool user_namespaces_available();

/// Whether `helper` is a usable setuid sandbox: owned by root, mode 4755.
bool suid_sandbox_usable(const std::string& helper);

/// The explanation the host gives when the sandbox can't start: what's missing and how to enable
/// it (the host never runs without its sandbox).
std::string sandbox_unavailable_message(const std::string& helper);

}  // namespace pico_desktop::host
