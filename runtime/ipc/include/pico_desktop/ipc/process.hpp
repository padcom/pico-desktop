// Starting the Pico backend with its stdin/stdout as the protocol link (ADR-0004) and moving
// framed messages over file descriptors: POSIX ones, or on Windows the C runtime's descriptors
// for the pipe handles (_open_osfhandle), so both sides read and write the same way.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "pico_desktop/ipc/frame.hpp"

namespace pico_desktop::ipc {

#if defined(_WIN32)
using ProcessId = std::uint32_t;
#else
using ProcessId = int;  // pid_t
#endif

struct ChildProcess {
  ProcessId pid = static_cast<ProcessId>(-1);
  int to_child = -1;    // the child's stdin
  int from_child = -1;  // the child's stdout
#if defined(_WIN32)
  void* handle = nullptr;  // the process's HANDLE (the caller closes it)
#endif
};

/// Starts `program` with `args`, its stdin and stdout piped to us (stderr is inherited, for logs).
/// The descriptors are close-on-exec, so later children don't inherit them.
std::optional<ChildProcess> spawn_piped(const std::string& program, const std::vector<std::string>& args,
                                        std::string& error);

#if !defined(_WIN32)
/// A pipe whose ends are close-on-exec (pipe2 where there is one).
bool cloexec_pipe(int fds[2]);
#endif

/// Writes all of `bytes` (retrying short writes and EINTR); false when the peer is gone.
bool write_all(int fd, std::span<const std::byte> bytes);

/// Accumulates bytes from a stream and cuts them into messages (the C++ side of framing).
class FrameBuffer {
 public:
  explicit FrameBuffer(std::size_t max_message = 4 * 1024 * 1024) : max_(max_message) {}

  void push(std::span<const std::byte> chunk);
  /// The next complete message; nothing until more bytes arrive or after an error.
  std::optional<std::vector<std::byte>> next();
  /// Set once a frame was invalid (zero or too long); the stream is unusable then.
  const std::string& error() const { return error_; }
  std::size_t pending() const { return buffer_.size() - start_; }

 private:
  std::vector<std::byte> buffer_;
  std::size_t start_ = 0;
  std::size_t max_;
  std::string error_;
};

/// One frame for `message`.
std::vector<std::byte> frame(std::span<const std::byte> message);

}  // namespace pico_desktop::ipc
