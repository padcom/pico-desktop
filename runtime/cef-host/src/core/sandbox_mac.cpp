// Inspecting Chromium's helper processes on macOS from the browser process: their arguments
// (`--type=`, KERN_PROCARGS2) and whether Seatbelt confines them (sandbox_check, #10).
#include "pico_desktop/host/sandbox.hpp"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <map>

// libsystem_sandbox (in libSystem): Chromium's Mac sandbox tests ask it the same way.
extern "C" int sandbox_check(pid_t pid, const char* operation, int type, ...);
#endif

namespace pico_desktop::host {

#if defined(__APPLE__)

namespace {

constexpr int kSandboxFilterNone = 0;  // SANDBOX_FILTER_NONE

std::map<int, int> parents() {
  std::map<int, int> parent;
  int mib[3] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL};
  size_t size = 0;
  if (sysctl(mib, 3, nullptr, &size, nullptr, 0) != 0) {
    return parent;
  }
  std::vector<kinfo_proc> procs(size / sizeof(kinfo_proc) + 16);
  size = procs.size() * sizeof(kinfo_proc);
  if (sysctl(mib, 3, procs.data(), &size, nullptr, 0) != 0) {
    return parent;
  }
  procs.resize(size / sizeof(kinfo_proc));
  for (const auto& p : procs) {
    parent[p.kp_proc.p_pid] = p.kp_eproc.e_ppid;
  }
  return parent;
}

std::string arguments(int pid) {
  int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
  int max = 0;
  int argmax_mib[2] = {CTL_KERN, KERN_ARGMAX};
  size_t size = sizeof(max);
  if (sysctl(argmax_mib, 2, &max, &size, nullptr, 0) != 0 || max <= 0) {
    return {};
  }
  std::string buffer(static_cast<std::size_t>(max), '\0');
  size = buffer.size();
  if (sysctl(mib, 3, buffer.data(), &size, nullptr, 0) != 0) {
    return {};
  }
  buffer.resize(size);
  return parse_procargs2(buffer);
}

}  // namespace

std::vector<MacProcessSandbox> inspect_descendants_mac(int root_pid) {
  std::vector<MacProcessSandbox> result;
  const auto parent = parents();
  for (const auto& [pid, ppid] : parent) {
    int p = ppid;
    for (int depth = 0; depth < 64 && p > 1 && p != root_pid; ++depth) {
      const auto it = parent.find(p);
      p = it == parent.end() ? 0 : it->second;
    }
    if (p != root_pid) {
      continue;
    }
    const std::string type = parse_cmdline_type(arguments(pid));
    if (type.empty()) {
      continue;  // not a Chromium helper (or gone already)
    }
    const int checked = sandbox_check(pid, nullptr, kSandboxFilterNone);
    result.push_back({pid, type, checked < 0 ? -1 : (checked != 0 ? 1 : 0)});
  }
  return result;
}

#else

std::vector<MacProcessSandbox> inspect_descendants_mac(int) { return {}; }

#endif

}  // namespace pico_desktop::host
