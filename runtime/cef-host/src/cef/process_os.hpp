// What the backend link needs from the operating system for one child process: noticing its
// exit, ending it, collecting its status, and unblocking the threads that read and write its
// pipes. POSIX (Linux: pidfd, macOS: kqueue) and Windows (the process HANDLE, CancelIoEx).
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include "pico_desktop/host/interruptible_io.hpp"
#include "pico_desktop/ipc/process.hpp"

namespace pico_desktop::host::cef::os {

/// A process's end: POSIX the wait status, Windows the whole 32-bit exit code; -1 when unknown.
using Status = std::int64_t;

/// Prepares the host (POSIX: a backend that dies mid-write gives EPIPE, not SIGPIPE).
void init_process_handling();

/// A descriptor that becomes readable when `child` exits (-1 where there's none).
int exit_watch(const ipc::ChildProcess& child);

/// Makes writes to the child's stdin non-blocking where the link waits for room (POSIX).
void prepare_pipes(const ipc::ChildProcess& child);

/// Ends `child` at once (Windows: with windows_killed_exit_code, so it reads "killed").
void kill(const ipc::ChildProcess& child);

/// Waits up to `grace` for `child` to exit, then kills it; its status (see describe), or -1.
Status reap(const ipc::ChildProcess& child, std::chrono::milliseconds grace);

/// "exit code 0", "signal 9", "killed", "exit code 0xC0000005", …
std::string describe(Status status);

/// Reads what's there (up to `size`, blocking until something is): the count, 0 at the end, -1
/// on an error (EINTR is retried).
long read_some(int fd, std::byte* data, std::size_t size);

/// Stops `thread`'s blocking pipe calls, which it makes inside `io` (enter/leave): none starts any
/// more, and the one it's in is cancelled, however late it blocks (Windows: CancelSynchronousIo,
/// repeated until the thread is out, #16). POSIX waits wake on the stop pipe instead; there it only
/// marks `io`. Never touches a descriptor, which may be closed by then.
void interrupt_io(std::thread& thread, InterruptibleIo& io);

/// How many bytes can be read from `fd` now without blocking (0 if unknown).
std::size_t bytes_available(int fd);

/// Windows: a thread that calls `on_exit` once `child` has exited, with its own handle to the
/// process (so reap may close child.handle meanwhile); the link learns of the exit even while
/// something the backend started still holds its stdout (#17). It ends once the process has.
/// POSIX: an empty thread (exit_watch's descriptor serves instead).
std::thread watch_exit(const ipc::ChildProcess& child, std::function<void()> on_exit);

void close_fd(int fd);

}  // namespace pico_desktop::host::cef::os
