// The backend's clipboard where it isn't implemented yet (macOS: NSPasteboard, Windows: the
// clipboard API, both later): every call answers UNAVAILABLE.
#include "clipboard.hpp"

namespace pico_desktop::host::cef::clipboard {

namespace {

Result unavailable() {
  return Result{std::nullopt, "UNAVAILABLE", "the backend's clipboard isn't implemented on this system yet"};
}

}  // namespace

void write_text(std::string, Done done) { done(unavailable()); }

void read_text(Done done) { done(unavailable()); }

void shutdown() {}

}  // namespace pico_desktop::host::cef::clipboard
