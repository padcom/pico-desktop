// What the backend link needs from the operating system for one child process: noticing its
// exit, ending it, collecting its status, and unblocking the threads that read and write its
// pipes. POSIX (Linux: pidfd, macOS: kqueue) and Windows (the process HANDLE, CancelIoEx).
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

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

/// Unblocks `thread` if it's waiting in a pipe read or write (Windows: CancelSynchronousIo; POSIX
/// waits wake on the stop pipe instead). Never touches a descriptor, which may be closed by then.
void cancel_blocking_io(std::thread& thread);

void close_fd(int fd);

}  // namespace pico_desktop::host::cef::os
