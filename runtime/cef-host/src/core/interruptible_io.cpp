#include "pico_desktop/host/interruptible_io.hpp"

#include <thread>

namespace pico_desktop::host {

// Both sides store, then load the other's flag (sequentially consistent): either the I/O thread
// sees `interrupted_` and makes no call, or interrupt() sees `in_call_` and keeps cancelling until
// the call is over, however late the thread actually blocks after enter().

bool InterruptibleIo::enter() {
  in_call_.store(true);
  if (interrupted_.load()) {
    in_call_.store(false);
    return false;
  }
  return true;
}

void InterruptibleIo::leave() { in_call_.store(false); }

bool InterruptibleIo::interrupt(const std::function<void()>& cancel, std::chrono::milliseconds every,
                                std::chrono::milliseconds budget) {
  interrupted_.store(true);
  const auto start = std::chrono::steady_clock::now();
  while (in_call_.load()) {
    // (In milliseconds: the default budget, milliseconds::max(), overflows as nanoseconds.)
    if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start) >= budget) {
      return false;
    }
    cancel();
    std::this_thread::sleep_for(every);
  }
  return true;
}

}  // namespace pico_desktop::host
