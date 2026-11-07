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

void InterruptibleIo::interrupt(const std::function<void()>& cancel, std::chrono::milliseconds every) {
  interrupted_.store(true);
  while (in_call_.load()) {
    cancel();
    std::this_thread::sleep_for(every);
  }
}

}  // namespace pico_desktop::host
