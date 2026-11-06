// The Pico Desktop native host. In F1 this becomes the CEF Views application (windows, assets,
// navigation policy, backend supervision, message routing: docs/architecture.md); for now it
// only identifies itself, so packaging and CI have a real binary to build and run.
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "pico_desktop/ipc/frame.hpp"

int main(int argc, char** argv) {
  const std::string_view arg = argc > 1 ? argv[1] : "";
  if (arg == "--version" || arg == "-V") {
    std::printf("pico-desktop-host %s (framing: %zu-byte headers, CEF: not linked)\n", PD_VERSION,
                pico_desktop::ipc::frame_header_bytes);
    return EXIT_SUCCESS;
  }
  if (arg.empty() || arg == "--help" || arg == "-h") {
    std::puts("Usage: pico-desktop-host [--version | --help]\n\nThe CEF host arrives in phase F1.");
    return arg.empty() ? EXIT_FAILURE : EXIT_SUCCESS;
  }
  std::fprintf(stderr, "pico-desktop-host: unknown option '%.*s'\n", static_cast<int>(arg.size()), arg.data());
  return EXIT_FAILURE;
}
