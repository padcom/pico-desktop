// Unit test for spawn_piped's failure paths (POSIX): nothing it opened is left behind (#7).
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>

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

int main() {
  second_pipe_fails_without_leaking();
  if (failures == 0) {
    std::puts("ipc.process: all checks passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
