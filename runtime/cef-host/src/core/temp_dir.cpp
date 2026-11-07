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

bool remove_temp_dir(const std::filesystem::path& dir, std::chrono::milliseconds budget, std::error_code& error) {
  const auto until = std::chrono::steady_clock::now() + budget;
  for (;;) {
    error.clear();
    fs::remove_all(dir, error);
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
