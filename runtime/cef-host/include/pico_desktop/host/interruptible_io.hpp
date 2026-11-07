// Stopping a thread that blocks in pipe reads or writes, where the OS can only cancel the call
// the thread is in at that moment (Windows: CancelSynchronousIo). A single cancel is lost if the
// thread is between calls; this protocol closes that window (#16).
#pragma once

#include <atomic>
#include <chrono>
#include <functional>

namespace pico_desktop::host {

class InterruptibleIo {
 public:
  /// The I/O thread, before each blocking call: false once interrupted (then it makes no call).
  bool enter();
  /// The I/O thread, once the call has returned.
  void leave();
  bool interrupted() const { return interrupted_.load(); }

  /// Another thread: no blocking call starts from now on, and `cancel` (which cancels the I/O
  /// thread's current call) is repeated, `every` apart, until the I/O thread has left any call it
  /// entered: true then. False if that hasn't happened within `budget` (the cancel isn't taking;
  /// the thread may still be in its call). Safe to call more than once and from several threads.
  bool interrupt(const std::function<void()>& cancel,
                 std::chrono::milliseconds every = std::chrono::milliseconds(1),
                 std::chrono::milliseconds budget = std::chrono::milliseconds::max());

 private:
  std::atomic<bool> interrupted_{false};
  std::atomic<bool> in_call_{false};
};

}  // namespace pico_desktop::host
