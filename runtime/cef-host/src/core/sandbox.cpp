#include "pico_desktop/host/sandbox.hpp"

#if defined(__linux__)
#include <sched.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

namespace pico_desktop::host {

namespace {

std::optional<int> to_int(std::string_view text) {
  int value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end == text.data()) {
    return std::nullopt;
  }
  return value;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return s;
}

[[maybe_unused]] std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

StatusFields parse_proc_status(std::string_view status) {
  StatusFields fields;
  while (!status.empty()) {
    const auto eol = status.find('\n');
    const std::string_view line = status.substr(0, eol);
    status = eol == std::string_view::npos ? std::string_view{} : status.substr(eol + 1);
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) {
      continue;
    }
    const auto key = line.substr(0, colon);
    const auto value = trim(line.substr(colon + 1));
    if (key == "Seccomp") {
      fields.seccomp_mode = to_int(value).value_or(-1);
    } else if (key == "NSpid") {
      int levels = 0;
      std::string_view rest = value;
      while (!rest.empty()) {
        const auto sep = rest.find_first_of(" \t");
        if (to_int(rest.substr(0, sep))) {
          ++levels;
        }
        rest = sep == std::string_view::npos ? std::string_view{} : trim(rest.substr(sep));
      }
      fields.nspid_levels = levels;
    }
  }
  return fields;
}

std::optional<int> parse_proc_stat_ppid(std::string_view stat) {
  // pid (comm) state ppid …: comm can hold anything, so look after its last ')'.
  const auto close = stat.rfind(')');
  if (close == std::string_view::npos || close + 2 >= stat.size()) {
    return std::nullopt;
  }
  std::string_view rest = stat.substr(close + 2);  // "<state> <ppid> …"
  const auto space = rest.find(' ');
  if (space == std::string_view::npos) {
    return std::nullopt;
  }
  rest = rest.substr(space + 1);
  return to_int(rest.substr(0, rest.find(' ')));
}

std::string parse_cmdline_type(std::string_view cmdline) {
  // Arguments are NUL-separated, except that Chromium rewrites its process titles
  // (setproctitle) into one space-separated string.
  constexpr std::string_view prefix = "--type=";
  while (!cmdline.empty()) {
    const auto end = cmdline.find_first_of(std::string_view("\0 ", 2));
    const auto arg = cmdline.substr(0, end);
    if (arg.starts_with(prefix)) {
      return std::string(arg.substr(prefix.size()));
    }
    cmdline = end == std::string_view::npos ? std::string_view{} : cmdline.substr(end + 1);
  }
  return {};
}

std::vector<ProcessSandbox> inspect_descendants([[maybe_unused]] int root_pid) {
  std::vector<ProcessSandbox> result;
#if defined(__linux__)
  namespace fs = std::filesystem;
  std::map<int, int> parent;
  std::error_code error;
  for (const auto& entry : fs::directory_iterator("/proc", error)) {
    const auto pid = to_int(entry.path().filename().string());
    if (!pid) {
      continue;
    }
    if (const auto ppid = parse_proc_stat_ppid(read_file(entry.path() / "stat"))) {
      parent[*pid] = *ppid;
    }
  }
  for (const auto& [pid, ppid] : parent) {
    // Is `pid` below root_pid?
    int p = ppid;
    for (int depth = 0; depth < 64 && p > 1 && p != root_pid; ++depth) {
      const auto it = parent.find(p);
      p = it == parent.end() ? 0 : it->second;
    }
    if (p != root_pid) {
      continue;
    }
    const fs::path dir = fs::path("/proc") / std::to_string(pid);
    const std::string type = parse_cmdline_type(read_file(dir / "cmdline"));
    if (type.empty()) {
      continue;  // not a Chromium child (or gone already)
    }
    const StatusFields fields = parse_proc_status(read_file(dir / "status"));
    result.push_back({pid, type, fields.seccomp_mode, fields.nspid_levels > 1});
  }
#endif
  return result;
}

bool renderers_sandboxed(const std::vector<ProcessSandbox>& processes) {
  bool any = false;
  for (const auto& p : processes) {
    if (p.type == "renderer") {
      any = true;
      if (p.seccomp_mode != 2 || !p.own_pid_namespace) {
        return false;
      }
    }
  }
  return any;
}

bool user_namespaces_available() {
#if defined(__linux__)
  const pid_t child = ::fork();
  if (child < 0) {
    return true;  // can't tell: let Chromium decide (and report)
  }
  if (child == 0) {
    ::_exit(::unshare(CLONE_NEWUSER) == 0 ? 0 : 1);
  }
  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) {
      return true;
    }
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
  return true;
#endif
}

bool suid_sandbox_usable(const std::string& helper) {
  struct stat st {};
  if (::stat(helper.c_str(), &st) != 0) {
    return false;
  }
  return st.st_uid == 0 && (st.st_mode & 07777) == 04755;
}

std::string sandbox_unavailable_message(const std::string& helper) {
  return "Chromium's sandbox can't start here: this system doesn't let unprivileged processes create user "
         "namespaces, and " + helper + " isn't a setuid-root sandbox helper (owned by root, mode 4755).\n"
         "The host never runs without its sandbox. To allow it, one of:\n"
         "  - Ubuntu 23.10+: sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0 (or an AppArmor\n"
         "    profile for this executable granting `userns`)\n"
         "  - Debian and others: sudo sysctl -w kernel.unprivileged_userns_clone=1, and a nonzero\n"
         "    user.max_user_namespaces\n"
         "  - or install the helper: sudo chown root " + helper + " && sudo chmod 4755 " + helper + "\n"
         "(see docs/platform-linux.md)";
}

}  // namespace pico_desktop::host
