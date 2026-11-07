#include "process_os.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/event.h>
#else
#include <sys/syscall.h>
#endif
#endif

#include <cerrno>

namespace pico_desktop::host::cef::os {

#if defined(_WIN32)

void init_process_handling() {}

int exit_watch(const ipc::ChildProcess&) { return -1; }

void prepare_pipes(const ipc::ChildProcess&) {}

void kill(const ipc::ChildProcess& child) { TerminateProcess(static_cast<HANDLE>(child.handle), 1); }

int reap(const ipc::ChildProcess& child, std::chrono::milliseconds grace) {
  const auto process = static_cast<HANDLE>(child.handle);
  if (WaitForSingleObject(process, static_cast<DWORD>(grace.count())) != WAIT_OBJECT_0) {
    TerminateProcess(process, 1);
    WaitForSingleObject(process, INFINITE);
  }
  DWORD code = 0;
  const bool ok = GetExitCodeProcess(process, &code) != 0;
  CloseHandle(process);
  return ok ? static_cast<int>(code & 0x7fffffff) : -1;
}

std::string describe(int status) {
  return status < 0 ? "an unknown status" : "exit code " + std::to_string(status);
}

long read_some(int fd, std::byte* data, std::size_t size) {
  return ::_read(fd, data, static_cast<unsigned>(size));
}

void cancel_blocking_io(std::thread& thread) {
  if (thread.joinable()) {
    CancelSynchronousIo(static_cast<HANDLE>(thread.native_handle()));
  }
}

void close_fd(int fd) { ::_close(fd); }

#else

void init_process_handling() { signal(SIGPIPE, SIG_IGN); }

int exit_watch(const ipc::ChildProcess& child) {
#if defined(__APPLE__)
  const int kq = ::kqueue();
  if (kq < 0) {
    return -1;
  }
  struct kevent change;
  EV_SET(&change, child.pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
  if (::kevent(kq, &change, 1, nullptr, 0, nullptr) != 0) {
    ::close(kq);
    return -1;
  }
  ::fcntl(kq, F_SETFD, FD_CLOEXEC);
  return kq;
#else
  return static_cast<int>(::syscall(SYS_pidfd_open, child.pid, 0));
#endif
}

void prepare_pipes(const ipc::ChildProcess& child) {
  ::fcntl(child.to_child, F_SETFL, ::fcntl(child.to_child, F_GETFL) | O_NONBLOCK);
}

void kill(const ipc::ChildProcess& child) { ::kill(child.pid, SIGKILL); }

int reap(const ipc::ChildProcess& child, std::chrono::milliseconds grace) {
  const auto until = std::chrono::steady_clock::now() + grace;
  int status = 0;
  for (;;) {
    const pid_t r = waitpid(child.pid, &status, WNOHANG);
    if (r == child.pid) {
      return status;
    }
    if (r < 0 && errno != EINTR) {
      return -1;
    }
    if (std::chrono::steady_clock::now() >= until) {
      break;
    }
    poll(nullptr, 0, 10);
  }
  ::kill(child.pid, SIGKILL);
  while (waitpid(child.pid, &status, 0) < 0) {
    if (errno != EINTR) {
      return -1;
    }
  }
  return status;
}

std::string describe(int status) {
  if (status < 0) {
    return "an unknown status";
  }
  if (WIFEXITED(status)) {
    return "exit code " + std::to_string(WEXITSTATUS(status));
  }
  if (WIFSIGNALED(status)) {
    return "signal " + std::to_string(WTERMSIG(status));
  }
  return "status " + std::to_string(status);
}

long read_some(int fd, std::byte* data, std::size_t size) {
  for (;;) {
    const ssize_t n = ::read(fd, data, size);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    return static_cast<long>(n);
  }
}

void cancel_blocking_io(std::thread&) {}

void close_fd(int fd) { ::close(fd); }

#endif

}  // namespace pico_desktop::host::cef::os
