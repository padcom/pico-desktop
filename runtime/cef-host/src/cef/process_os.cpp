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
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/event.h>
#else
#include <sys/syscall.h>
#endif
#endif

#include <cerrno>

#include "pico_desktop/host/exit_code.hpp"

namespace pico_desktop::host::cef::os {

#if defined(_WIN32)

void init_process_handling() {}

int exit_watch(const ipc::ChildProcess&) { return -1; }

void prepare_pipes(const ipc::ChildProcess&) {}

void kill(const ipc::ChildProcess& child) {
  TerminateProcess(static_cast<HANDLE>(child.handle), windows_killed_exit_code);
}

Status reap(const ipc::ChildProcess& child, std::chrono::milliseconds grace) {
  const auto process = static_cast<HANDLE>(child.handle);
  if (WaitForSingleObject(process, static_cast<DWORD>(grace.count())) != WAIT_OBJECT_0) {
    TerminateProcess(process, windows_killed_exit_code);
    WaitForSingleObject(process, INFINITE);
  }
  DWORD code = 0;
  const bool ok = GetExitCodeProcess(process, &code) != 0;
  CloseHandle(process);
  return ok ? static_cast<Status>(code) : -1;  // all 32 bits: NTSTATUS crash codes have the top one set
}

std::string describe(Status status) {
  return status < 0 ? "an unknown status" : describe_windows_exit_code(static_cast<std::uint32_t>(status));
}

long read_some(int fd, std::byte* data, std::size_t size) {
  return ::_read(fd, data, static_cast<unsigned>(size));
}

void interrupt_io(std::thread& thread, InterruptibleIo& io) {
  if (!thread.joinable()) {
    io.interrupt([] {});
    return;
  }
  const auto handle = static_cast<HANDLE>(thread.native_handle());
  io.interrupt([handle] { CancelSynchronousIo(handle); });
}

std::size_t bytes_available(int fd) {
  const auto pipe = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
  DWORD available = 0;
  if (pipe == INVALID_HANDLE_VALUE || !PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
    return 0;
  }
  return available;
}

std::thread watch_exit(const ipc::ChildProcess& child, std::function<void()> on_exit) {
  HANDLE process = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), static_cast<HANDLE>(child.handle), GetCurrentProcess(), &process,
                       SYNCHRONIZE, FALSE, 0)) {
    return {};  // then only EOF tells, as before
  }
  return std::thread([process, on_exit = std::move(on_exit)] {
    const bool exited = WaitForSingleObject(process, INFINITE) == WAIT_OBJECT_0;
    CloseHandle(process);
    if (exited) {
      on_exit();
    }
  });
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

Status reap(const ipc::ChildProcess& child, std::chrono::milliseconds grace) {
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

std::string describe(Status wide) {
  if (wide < 0) {
    return "an unknown status";
  }
  const int status = static_cast<int>(wide);
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

void interrupt_io(std::thread&, InterruptibleIo& io) { io.interrupt([] {}); }

std::size_t bytes_available(int fd) {
  int available = 0;
  return ::ioctl(fd, FIONREAD, &available) == 0 && available > 0 ? static_cast<std::size_t>(available) : 0;
}

std::thread watch_exit(const ipc::ChildProcess&, std::function<void()>) { return {}; }

void close_fd(int fd) { ::close(fd); }

#endif

}  // namespace pico_desktop::host::cef::os
