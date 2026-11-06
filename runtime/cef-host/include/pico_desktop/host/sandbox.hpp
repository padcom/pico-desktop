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

}  // namespace pico_desktop::host
