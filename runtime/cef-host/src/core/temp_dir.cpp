#include "pico_desktop/host/temp_dir.hpp"

#if defined(_WIN32)
#include <process.h>

#include <chrono>
#else
#include <stdlib.h>  // mkdtemp (glibc)
#include <unistd.h>  // mkdtemp (macOS)
#endif

#include <system_error>
#include <thread>

namespace pico_desktop::host {

namespace fs = std::filesystem;

std::filesystem::path make_private_temp_dir(const std::string& prefix) {
  std::error_code error;
  const fs::path temp = fs::temp_directory_path(error);
  if (error) {
    return {};
  }
#if defined(_WIN32)
  // No mkdtemp; %TEMP% is per user, so only uniqueness matters.
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = temp / (prefix + std::to_string(::_getpid()) + "-" + std::to_string(stamp + attempt));
    if (fs::create_directory(dir, error)) {
      return dir;
    }
  }
  return {};
#else
  std::string pattern = (temp / (prefix + "XXXXXX")).string();
  if (char* made = ::mkdtemp(pattern.data())) {
    return made;
  }
  return {};
#endif
}

namespace {

/// Removes what it can below and including `path`, going on past what it can't (unlike
/// fs::remove_all, which stops at the first failure); the first failure is kept in `error`.
void remove_all_it_can(const fs::path& path, std::error_code& error) {
  std::error_code status_error;
  const auto status = fs::symlink_status(path, status_error);
  if (status_error || status.type() == fs::file_type::not_found) {
    return;
  }
  if (status.type() == fs::file_type::directory) {
    std::error_code list_error;
    for (fs::directory_iterator it(path, list_error), end; !list_error && it != end; it.increment(list_error)) {
      remove_all_it_can(it->path(), error);
    }
    if (list_error && !error) {
      error = list_error;
    }
  }
  std::error_code remove_error;
  fs::remove(path, remove_error);
  if (remove_error && !error) {
    error = remove_error;
  }
}

}  // namespace

bool remove_temp_dir(const std::filesystem::path& dir, std::chrono::milliseconds budget, std::error_code& error) {
  const auto until = std::chrono::steady_clock::now() + budget;
  for (;;) {
    error.clear();
    remove_all_it_can(dir, error);
    if (!error && !fs::exists(dir, error)) {
      error.clear();
      return true;
    }
    if (std::chrono::steady_clock::now() >= until) {
      if (!error) {
        error = std::make_error_code(std::errc::directory_not_empty);
      }
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

}  // namespace pico_desktop::host
