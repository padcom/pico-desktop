// Starting the Pico backend with its stdin/stdout as the protocol link (ADR-0004) and moving
// framed messages over file descriptors. POSIX for now (Linux is the priority target).
#pragma once

#include <sys/types.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "pico_desktop/ipc/frame.hpp"

namespace pico_desktop::ipc {

struct ChildProcess {
  pid_t pid = -1;
  int to_child = -1;    // the child's stdin
  int from_child = -1;  // the child's stdout
};

/// Starts `program` with `args`, its stdin and stdout piped to us (stderr is inherited, for logs).
/// The descriptors are close-on-exec, so later children don't inherit them.
std::optional<ChildProcess> spawn_piped(const std::string& program, const std::vector<std::string>& args,
                                        std::string& error);

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
