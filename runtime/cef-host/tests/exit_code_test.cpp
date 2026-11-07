// Unit test for describe_windows_exit_code (#19, #20): an NTSTATUS crash keeps all 32 bits and
// reads in hex, the host's own kill reads "killed", and ordinary codes stay decimal.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "pico_desktop/host/exit_code.hpp"

using namespace pico_desktop::host;

static int failures = 0;

static void expect(std::uint32_t code, const std::string& want) {
  const std::string got = describe_windows_exit_code(code);
  if (got != want) {
    std::fprintf(stderr, "FAIL: 0x%08X reads \"%s\", want \"%s\"\n", static_cast<unsigned>(code), got.c_str(),
                 want.c_str());
    ++failures;
  }
}

int main() {
  expect(0, "exit code 0");
  expect(1, "exit code 1");  // a backend's own failure: not to be mistaken for a kill
  expect(3, "exit code 3");
  expect(259, "exit code 259");
  expect(0xC0000005u, "exit code 0xC0000005");  // access violation
  expect(0xC00000FDu, "exit code 0xC00000FD");  // stack overflow
  expect(0xC0000409u, "exit code 0xC0000409");  // fail fast (abort, /GS)
  expect(0x80000003u, "exit code 0x80000003");  // breakpoint
  expect(0xFFFFFFFFu, "exit code 0xFFFFFFFF");  // exit(-1)
  expect(windows_killed_exit_code, "killed");
  if (windows_killed_exit_code == 1) {
    std::fputs("FAIL: the kill code is the same as an ordinary failure's\n", stderr);
    ++failures;
  }
  if (failures == 0) {
    std::puts("host.exit-code: all checks passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
