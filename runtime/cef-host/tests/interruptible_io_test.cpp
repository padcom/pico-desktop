// Unit test for InterruptibleIo (#16): a thread blocked in a pipe read that nothing will ever
// satisfy is always stopped, including when the interrupt comes while it's between calls (where
// one cancel would be lost and the next read would block for good). The cancel is the real one:
// Windows CancelSynchronousIo; POSIX a signal without SA_RESTART (the read fails with EINTR).
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#endif

#include "pico_desktop/host/interruptible_io.hpp"

using pico_desktop::host::InterruptibleIo;
using namespace std::chrono_literals;

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

namespace {

/// A pipe nobody writes to: a read from it blocks until it's cancelled.
struct SilentPipe {
#if defined(_WIN32)
  HANDLE read = nullptr, write = nullptr;
  SilentPipe() { CreatePipe(&read, &write, nullptr, 0); }
  ~SilentPipe() {
    CloseHandle(read);
    CloseHandle(write);
  }
  void blocking_read() const {
    char byte;
    DWORD n = 0;
    ReadFile(read, &byte, 1, &n, nullptr);
  }
#else
  int fds[2] = {-1, -1};
  SilentPipe() {
    if (pipe(fds) != 0) {
      std::perror("pipe");
    }
  }
  ~SilentPipe() {
    close(fds[0]);
    close(fds[1]);
  }
  void blocking_read() const {
    char byte;
    [[maybe_unused]] const auto n = ::read(fds[0], &byte, 1);
  }
#endif
};

std::function<void()> canceller(std::thread& thread) {
#if defined(_WIN32)
  const HANDLE handle = static_cast<HANDLE>(thread.native_handle());
  return [handle] { CancelSynchronousIo(handle); };
#else
  const pthread_t handle = thread.native_handle();
  return [handle] { pthread_kill(handle, SIGUSR1); };
#endif
}

/// The reader loop the link runs: enter, read, leave, until interrupted. `between` is how long it
/// spends between reads (dispatching what it read), where a lone cancel finds nothing to cancel.
/// True if interrupt() stopped it within the deadline.
bool stops(std::chrono::milliseconds between, bool interrupt_while_between) {
  SilentPipe pipe;
  InterruptibleIo io;
  std::atomic<bool> entered{false};
  std::promise<void> done;
  auto finished = done.get_future();
  std::thread reader([&] {
    for (bool first = true; io.enter(); first = false) {
      if (first) {
        entered = true;
      }
      std::this_thread::sleep_for(between);  // entered, but not yet blocked in the read
      pipe.blocking_read();
      io.leave();
    }
    done.set_value();
  });
  while (!entered) {
    std::this_thread::sleep_for(1ms);
  }
  if (!interrupt_while_between) {
    std::this_thread::sleep_for(between + 50ms);  // now it's blocked in the read
  }
  io.interrupt(canceller(reader));
  const bool stopped = finished.wait_for(5s) == std::future_status::ready;
  if (stopped) {
    reader.join();
  } else {
    reader.detach();  // stuck for good: that's the failure; the process exits anyway
  }
  return stopped;
}

}  // namespace

int main() {
#if !defined(_WIN32)
  struct sigaction action {};
  action.sa_handler = [](int) {};
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;  // no SA_RESTART: a blocked read returns EINTR
  sigaction(SIGUSR1, &action, nullptr);
#endif
  check(stops(0ms, false), "a thread blocked in a read is stopped");
  check(stops(100ms, true), "a thread interrupted between calls is stopped (#16)");

  // A cancel that never takes: interrupt() gives up after its budget and says so; once the
  // thread is out of its call, it succeeds.
  {
    InterruptibleIo stuck;
    check(stuck.enter(), "a call starts");
    const auto start = std::chrono::steady_clock::now();
    check(!stuck.interrupt([] {}, 1ms, 50ms), "false when the cancel doesn't take within the budget");
    check(std::chrono::steady_clock::now() - start < 2s, "within about the budget");
    stuck.leave();
    check(stuck.interrupt([] {}, 1ms, 50ms), "true once the thread is out of its call");
  }

  InterruptibleIo io;
  io.interrupt([] {});
  check(io.interrupted(), "interrupted() says so");
  check(!io.enter(), "no call starts once interrupted");
  io.interrupt([] {});  // again: returns at once

  if (failures == 0) {
    std::puts("host.interruptible-io: all checks passed");
  }
  std::fflush(stdout);
  std::_Exit(failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);  // even with a stuck thread
}
