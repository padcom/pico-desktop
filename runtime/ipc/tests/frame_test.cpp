// Unit test for the frame header (no test framework yet: one is chosen with the F2 IPC work).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "pico_desktop/ipc/frame.hpp"

namespace ipc = pico_desktop::ipc;

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  const auto header = ipc::encode_frame_header(0x01020304);
  check(header[0] == std::byte{1} && header[3] == std::byte{4}, "big-endian byte order");

  for (const std::uint32_t length : {0u, 1u, 65536u, 0xffffffffu}) {
    const auto encoded = ipc::encode_frame_header(length);
    check(ipc::decode_frame_header(encoded) == length, "round trip");
  }

  const std::vector<std::byte> partial(3, std::byte{0});
  check(!ipc::decode_frame_header(partial).has_value(), "partial header waits for more bytes");

  std::vector<std::byte> with_payload(ipc::frame_header_bytes + 2, std::byte{0xff});
  const auto h = ipc::encode_frame_header(2);
  std::copy(h.begin(), h.end(), with_payload.begin());
  check(ipc::decode_frame_header(with_payload) == 2u, "header followed by payload bytes");

  if (failures == 0) {
    std::puts("ipc.frame: all checks passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
