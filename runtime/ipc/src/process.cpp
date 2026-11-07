#include "pico_desktop/ipc/process.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

#if !defined(_WIN32)
extern char** environ;
#endif

namespace pico_desktop::ipc {

#if defined(_WIN32)

namespace {

std::wstring wide(const std::string& utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
  return out;
}

/// One argument as CommandLineToArgvW (and the C runtime) splits it back.
void append_quoted(std::wstring& line, const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    line += arg;
    return;
  }
  line += L'"';
  for (std::size_t i = 0;; ++i) {
    std::size_t backslashes = 0;
    while (i < arg.size() && arg[i] == L'\\') {
      ++i;
      ++backslashes;
    }
    if (i == arg.size()) {
      line.append(backslashes * 2, L'\\');  // before the closing quote, they're doubled
      break;
    }
    if (arg[i] == L'"') {
      line.append(backslashes * 2 + 1, L'\\');
    } else {
      line.append(backslashes, L'\\');
    }
    line += arg[i];
  }
  line += L'"';
}

std::string last_error(const char* what) {
  return std::string(what) + ": Windows error " + std::to_string(GetLastError());
}

/// Owns a HANDLE: closed when the owner goes, unless released to a new owner.
class UniqueHandle {
 public:
  UniqueHandle() = default;
  explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;
  ~UniqueHandle() { reset(); }

  HANDLE get() const { return handle_; }
  /// For an API that writes a new handle here (whatever was owned is closed first).
  HANDLE* out() {
    reset();
    return &handle_;
  }
  HANDLE release() { return std::exchange(handle_, nullptr); }
  void reset() {
    if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
    handle_ = nullptr;
  }

 private:
  HANDLE handle_ = nullptr;
};

/// A PROC_THREAD_ATTRIBUTE_LIST that only lets the child inherit the given handles; deleted when
/// it goes.
class InheritOnly {
 public:
  InheritOnly() = default;
  InheritOnly(const InheritOnly&) = delete;
  InheritOnly& operator=(const InheritOnly&) = delete;
  ~InheritOnly() {
    if (initialized_) {
      DeleteProcThreadAttributeList(list());
    }
  }

  /// False (with the error in `error`) unless the list holds exactly `handles`: without it,
  /// bInheritHandles would hand the child every inheritable handle of this process (#6).
  bool set(std::vector<HANDLE>& handles, std::string& error) {
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);  // fails by design; says the size
    if (size == 0) {
      error = last_error("InitializeProcThreadAttributeList");
      return false;
    }
    storage_.resize(size);
    if (!InitializeProcThreadAttributeList(list(), 1, 0, &size)) {
      error = last_error("InitializeProcThreadAttributeList");
      return false;
    }
    initialized_ = true;
    if (!UpdateProcThreadAttribute(list(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(),
                                   handles.size() * sizeof(HANDLE), nullptr, nullptr)) {
      error = last_error("UpdateProcThreadAttribute");
      return false;
    }
    return true;
  }

  LPPROC_THREAD_ATTRIBUTE_LIST list() { return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data()); }

 private:
  std::vector<std::byte> storage_;
  bool initialized_ = false;
};

/// A C runtime descriptor that takes over `handle` (released to it), or -1 (`handle` kept).
int adopt_as_descriptor(UniqueHandle& handle, int flags) {
  const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle.get()), flags);
  if (fd != -1) {
    handle.release();
  }
  return fd;
}

}  // namespace

std::optional<ChildProcess> spawn_piped(const std::string& program, const std::vector<std::string>& args,
                                        std::string& error) {
  SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  // Whatever is still owned here when spawn_piped returns is closed (#7, #8).
  UniqueHandle in_read, in_write, out_read, out_write;
  if (!CreatePipe(in_read.out(), in_write.out(), &inheritable, 0) ||
      !CreatePipe(out_read.out(), out_write.out(), &inheritable, 0)) {
    error = last_error("CreatePipe");
    return std::nullopt;
  }
  // Our ends stay ours; the child's ends are the only handles it inherits (with stderr).
  SetHandleInformation(in_write.get(), HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_read.get(), HANDLE_FLAG_INHERIT, 0);
  // An inheritable copy of our stderr for this child only: our own stderr's flags stay as they
  // are, so later CreateProcess calls (CEF's too) don't inherit it (#9).
  UniqueHandle err;
  const HANDLE own_err = GetStdHandle(STD_ERROR_HANDLE);
  std::vector<HANDLE> inherited{in_read.get(), out_write.get()};
  if (own_err != nullptr && own_err != INVALID_HANDLE_VALUE &&
      DuplicateHandle(GetCurrentProcess(), own_err, GetCurrentProcess(), err.out(), 0, TRUE, DUPLICATE_SAME_ACCESS)) {
    inherited.push_back(err.get());
  }
  InheritOnly attributes;
  if (!attributes.set(inherited, error)) {
    error = "spawn " + program + ": " + error;
    return std::nullopt;
  }
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = in_read.get();
  startup.StartupInfo.hStdOutput = out_write.get();
  startup.StartupInfo.hStdError = err.get();
  startup.lpAttributeList = attributes.list();
  const std::wstring application = wide(program);
  std::wstring line;
  append_quoted(line, application);
  for (const auto& a : args) {
    line += L' ';
    append_quoted(line, wide(a));
  }
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT,
                      nullptr, nullptr, &startup.StartupInfo, &process)) {
    error = "spawn " + program + ": " + last_error("CreateProcess");
    return std::nullopt;
  }
  CloseHandle(process.hThread);
  UniqueHandle child(process.hProcess);
  const int to_child = adopt_as_descriptor(in_write, _O_WRONLY | _O_BINARY);
  const int from_child = to_child == -1 ? -1 : adopt_as_descriptor(out_read, _O_RDONLY | _O_BINARY);
  if (from_child == -1) {
    // A child we can't talk to: stopped, and nothing of it is kept (#8).
    error = "spawn " + program + ": _open_osfhandle: errno " + std::to_string(errno);
    if (to_child != -1) {
      _close(to_child);
    }
    TerminateProcess(child.get(), static_cast<UINT>(ERROR_INVALID_HANDLE));
    WaitForSingleObject(child.get(), 5000);
    return std::nullopt;
  }
  return ChildProcess{process.dwProcessId, to_child, from_child, child.release()};
}

bool write_all(int fd, std::span<const std::byte> bytes) {
  std::size_t done = 0;
  while (done < bytes.size()) {
    const std::size_t chunk = std::min<std::size_t>(bytes.size() - done, 1u << 30);
    const int n = ::_write(fd, bytes.data() + done, static_cast<unsigned>(chunk));
    if (n <= 0) {
      return false;
    }
    done += static_cast<std::size_t>(n);
  }
  return true;
}

#else

namespace {

/// Closes both ends of a pipe (those that are open), keeping errno for the caller's message.
void close_pipe(int fds[2]) {
  const int saved = errno;
  for (int i = 0; i < 2; ++i) {
    if (fds[i] >= 0) {
      close(fds[i]);
      fds[i] = -1;
    }
  }
  errno = saved;
}

}  // namespace

/// A pipe whose ends don't leak into other programs this process starts. On failure nothing is
/// left open and errno says why.
bool cloexec_pipe(int fds[2]) {
#if defined(__APPLE__)
  // No pipe2 on macOS; between pipe and fcntl another thread's spawn could inherit the ends, but
  // spawns here say POSIX_SPAWN_CLOEXEC_DEFAULT, which closes everything not named.
  if (pipe(fds) != 0) {
    return false;
  }
  if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) == 0 && fcntl(fds[1], F_SETFD, FD_CLOEXEC) == 0) {
    return true;
  }
  close_pipe(fds);
  return false;
#else
  return pipe2(fds, O_CLOEXEC) == 0;
#endif
}

std::optional<ChildProcess> spawn_piped(const std::string& program, const std::vector<std::string>& args,
                                        std::string& error) {
  int in[2] = {-1, -1};
  int out[2] = {-1, -1};
  if (!cloexec_pipe(in)) {
    error = std::string("pipe: ") + std::strerror(errno);
    return std::nullopt;
  }
  if (!cloexec_pipe(out)) {
    error = std::string("pipe: ") + std::strerror(errno);
    close_pipe(in);
    return std::nullopt;
  }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, in[0], 0);
  posix_spawn_file_actions_adddup2(&actions, out[1], 1);
#if defined(__APPLE__)
  posix_spawn_file_actions_addinherit_np(&actions, 2);  // stderr stays, for logs
#endif
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
  short flags = POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK;
#if defined(__APPLE__)
  flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
  posix_spawnattr_setflags(&attributes, flags);
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

#endif

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
