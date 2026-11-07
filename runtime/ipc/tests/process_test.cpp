// Unit test for spawn_piped: on failure nothing it opened is left behind (#7, POSIX); on Windows
// it leaves the host's own stderr as it was (#9) and returns usable descriptors (#8).
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <windows.h>
#else
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "pico_desktop/ipc/process.hpp"

namespace ipc = pico_desktop::ipc;

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

#if !defined(_WIN32)

/// The descriptor numbers below `limit` that are free.
static std::vector<int> free_fds_below(int limit) {
  std::vector<int> free;
  for (int fd = 0; fd < limit; ++fd) {
    if (fcntl(fd, F_GETFD) == -1) {
      free.push_back(fd);
    }
  }
  return free;
}

/// The second pipe can't be made: exactly three descriptors are free, the first pipe takes two.
static void second_pipe_fails_without_leaking() {
  rlimit original{};
  if (getrlimit(RLIMIT_NOFILE, &original) != 0) {
    check(false, "getrlimit");
    return;
  }
  int limit = 0;
  while (free_fds_below(limit).size() < 3) {
    ++limit;
  }
  const auto before = free_fds_below(limit);

  rlimit tight = original;
  tight.rlim_cur = static_cast<rlim_t>(limit);
  if (setrlimit(RLIMIT_NOFILE, &tight) != 0) {
    check(false, "setrlimit");
    return;
  }
  std::string error;
  const auto child = ipc::spawn_piped("/bin/true", {}, error);
  setrlimit(RLIMIT_NOFILE, &original);

  check(!child.has_value(), "spawn fails when the second pipe can't be made");
  check(error.rfind("pipe: ", 0) == 0, "the error names the pipe");
  check(free_fds_below(limit) == before, "the first pipe's ends are closed again");
}

#else

/// spawn_piped hands the child an inheritable copy of stderr; the host's own handle isn't made
/// inheritable, or every later CreateProcess (CEF's too) would inherit it.
static void our_stderr_stays_uninheritable() {
  const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
  DWORD original = 0;
  if (err == nullptr || err == INVALID_HANDLE_VALUE || !GetHandleInformation(err, &original)) {
    std::puts("ipc.process: no stderr handle; the stderr check is skipped");
    return;
  }
  SetHandleInformation(err, HANDLE_FLAG_INHERIT, 0);
  char system[MAX_PATH] = {};
  GetSystemDirectoryA(system, MAX_PATH);
  std::string error;
  const auto child = ipc::spawn_piped(std::string(system) + "\\cmd.exe", {"/c", "exit", "0"}, error);
  DWORD after = 0;
  GetHandleInformation(err, &after);
  SetHandleInformation(err, HANDLE_FLAG_INHERIT, original & HANDLE_FLAG_INHERIT);

  check(child.has_value(), "cmd.exe starts");
  check((after & HANDLE_FLAG_INHERIT) == 0, "the host's stderr isn't made inheritable");
  if (child) {
    check(child->to_child != -1 && child->from_child != -1, "the child's stdin and stdout are descriptors");
    check(child->handle != nullptr, "the child's process handle");
    WaitForSingleObject(static_cast<HANDLE>(child->handle), 10000);
    CloseHandle(static_cast<HANDLE>(child->handle));
    _close(child->to_child);
    _close(child->from_child);
  } else {
    std::fprintf(stderr, "spawn: %s\n", error.c_str());
  }
}

#endif

int main() {
#if defined(_WIN32)
  our_stderr_stays_uninheritable();
#else
  second_pipe_fails_without_leaking();
#endif
  if (failures == 0) {
    std::puts("ipc.process: all checks passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
