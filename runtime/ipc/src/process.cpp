#include "pico_desktop/ipc/process.hpp"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

extern char** environ;

namespace pico_desktop::ipc {

std::optional<ChildProcess> spawn_piped(const std::string& program, const std::vector<std::string>& args,
                                        std::string& error) {
  int in[2] = {-1, -1};
  int out[2] = {-1, -1};
  if (pipe2(in, O_CLOEXEC) != 0 || pipe2(out, O_CLOEXEC) != 0) {
    error = std::string("pipe: ") + std::strerror(errno);
    return std::nullopt;
  }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, in[0], 0);
  posix_spawn_file_actions_adddup2(&actions, out[1], 1);
  std::vector<std::string> argv_storage{program};
  argv_storage.insert(argv_storage.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (auto& a : argv_storage) {
    argv.push_back(a.data());
  }
  argv.push_back(nullptr);
  // The child starts with default signal handling and nothing blocked, whatever the host (and
  // Chromium) changed: a backend whose stdout reader is gone gets SIGPIPE as usual.
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
  sigset_t defaults;
  sigemptyset(&defaults);
  sigaddset(&defaults, SIGPIPE);
  sigaddset(&defaults, SIGTERM);
  sigaddset(&defaults, SIGINT);
  sigset_t none;
  sigemptyset(&none);
  posix_spawnattr_setsigdefault(&attributes, &defaults);
  posix_spawnattr_setsigmask(&attributes, &none);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
  pid_t pid = -1;
  const int rc = posix_spawn(&pid, program.c_str(), &actions, &attributes, argv.data(), environ);
  posix_spawnattr_destroy(&attributes);
  posix_spawn_file_actions_destroy(&actions);
  close(in[0]);
  close(out[1]);
  if (rc != 0) {
    close(in[1]);
    close(out[0]);
    error = "spawn " + program + ": " + std::strerror(rc);
    return std::nullopt;
  }
  return ChildProcess{pid, in[1], out[0]};
}

bool write_all(int fd, std::span<const std::byte> bytes) {
  std::size_t done = 0;
  while (done < bytes.size()) {
    const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      return false;
    }
    done += static_cast<std::size_t>(n);
  }
  return true;
}

void FrameBuffer::push(std::span<const std::byte> chunk) {
  if (start_ > 0 && start_ == buffer_.size()) {
    buffer_.clear();
    start_ = 0;
  } else if (start_ > (1 << 20) && start_ > buffer_.size() / 2) {
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(start_));
    start_ = 0;
  }
  buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());
}

std::optional<std::vector<std::byte>> FrameBuffer::next() {
  if (!error_.empty() || pending() < frame_header_bytes) {
    return std::nullopt;
  }
  const auto length = *decode_frame_header(std::span(buffer_).subspan(start_));
  if (length == 0 || length > max_) {
    error_ = length == 0 ? "a zero-length frame" : "a frame over the size limit";
    return std::nullopt;
  }
  if (pending() - frame_header_bytes < length) {
    return std::nullopt;
  }
  const auto begin = buffer_.begin() + static_cast<std::ptrdiff_t>(start_ + frame_header_bytes);
  std::vector<std::byte> message(begin, begin + length);
  start_ += frame_header_bytes + length;
  return message;
}

std::vector<std::byte> frame(std::span<const std::byte> message) {
  const auto header = encode_frame_header(static_cast<std::uint32_t>(message.size()));
  std::vector<std::byte> out(header.begin(), header.end());
  out.insert(out.end(), message.begin(), message.end());
  return out;
}

}  // namespace pico_desktop::ipc
