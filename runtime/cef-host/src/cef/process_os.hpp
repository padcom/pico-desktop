// What the backend link needs from the operating system for one child process: noticing its
// exit, ending it, collecting its status, and unblocking the threads that read and write its
// pipes. POSIX (Linux: pidfd, macOS: kqueue) and Windows (the process HANDLE, CancelIoEx).
#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include "pico_desktop/ipc/process.hpp"

namespace pico_desktop::host::cef::os {

/// Prepares the host (POSIX: a backend that dies mid-write gives EPIPE, not SIGPIPE).
void init_process_handling();

/// A descriptor that becomes readable when `child` exits (-1 where there's none).
int exit_watch(const ipc::ChildProcess& child);

/// Makes writes to the child's stdin non-blocking where the link waits for room (POSIX).
void prepare_pipes(const ipc::ChildProcess& child);

/// Ends `child` at once.
void kill(const ipc::ChildProcess& child);

/// Waits up to `grace` for `child` to exit, then kills it; its status (see describe), or -1.
int reap(const ipc::ChildProcess& child, std::chrono::milliseconds grace);

/// "exit code 0", "signal 9", …
std::string describe(int status);

/// Reads what's there (up to `size`, blocking until something is): the count, 0 at the end, -1
/// on an error (EINTR is retried).
long read_some(int fd, std::byte* data, std::size_t size);

/// Unblocks a thread waiting to read or write `fd` (Windows; POSIX waits wake on the stop pipe).
void cancel_io(int fd);

void close_fd(int fd);

}  // namespace pico_desktop::host::cef::os
