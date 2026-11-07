// Unit test for make_private_temp_dir (the self-test's throwaway profile, #27): a new directory
// in the temporary directory, and on POSIX only its owner's (0700) with a name nobody can guess.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "pico_desktop/host/temp_dir.hpp"

namespace fs = std::filesystem;
using pico_desktop::host::make_private_temp_dir;
using pico_desktop::host::remove_temp_dir;
using namespace std::chrono_literals;

static int failures = 0;

#define CHECK(expr)                                                         \
  do {                                                                      \
    if (!(expr)) {                                                          \
      std::fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #expr); \
      ++failures;                                                           \
    }                                                                       \
  } while (0)

int main() {
#if !defined(_WIN32)
  ::umask(022);  // a usual umask: plain create_directory would make the directory 0755
#endif
  const std::string prefix = "pd-temp-dir-test-";
  const fs::path first = make_private_temp_dir(prefix);
  const fs::path second = make_private_temp_dir(prefix);

  CHECK(!first.empty() && fs::is_directory(first));
  CHECK(!second.empty() && fs::is_directory(second));
  CHECK(first != second);
  CHECK(first.parent_path() == fs::temp_directory_path() ||
        fs::equivalent(first.parent_path(), fs::temp_directory_path()));
  CHECK(first.filename().string().rfind(prefix, 0) == 0);

#if !defined(_WIN32)
  // Only the owner may list, create or remove anything in it.
  CHECK((fs::status(first).permissions() & fs::perms::all) == fs::perms::owner_all);
  // Random (mkdtemp), not derived from the process id: another user can't predict it and
  // create it first.
  const std::string suffix = first.filename().string().substr(prefix.size());
  CHECK(suffix.rfind(std::to_string(::getpid()) + "-", 0) != 0);
#endif

  std::error_code ignored;
  fs::remove_all(first, ignored);
  fs::remove_all(second, ignored);

  // remove_temp_dir (#23): the whole tree, and a missing one counts as removed.
  {
    const fs::path dir = make_private_temp_dir(prefix);
    fs::create_directories(dir / "Default" / "Cache");
    std::ofstream(dir / "Default" / "Cache" / "data") << "x";
    std::error_code error;
    CHECK(remove_temp_dir(dir, 1s, error) && !error);
    CHECK(!fs::exists(dir));
    CHECK(remove_temp_dir(dir, 0ms, error) && !error);
  }
#if defined(_WIN32)
  // A file still open in it (as a Chromium subprocess holds one after CefShutdown): the removal
  // waits until it's closed instead of leaving the profile behind.
  {
    const fs::path dir = make_private_temp_dir(prefix);
    const fs::path file = dir / "LOCK";
    std::ofstream(file) << "x";
    const HANDLE held = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(held != INVALID_HANDLE_VALUE);
    std::error_code error;
    CHECK(!remove_temp_dir(dir, 200ms, error) && error);  // still held: fails, and says why
    std::thread release([held] {
      std::this_thread::sleep_for(500ms);
      CloseHandle(held);
    });
    CHECK(remove_temp_dir(dir, 10s, error) && !error);
    release.join();
    CHECK(!fs::exists(dir));
  }
#else
  // A directory that can't be emptied (its parent is read-only): false within the budget, with
  // the error. (Root may remove anyway; then there's nothing to check.)
  if (::geteuid() != 0) {
    const fs::path dir = make_private_temp_dir(prefix);
    fs::create_directories(dir / "locked");
    std::ofstream(dir / "locked" / "file") << "x";
    fs::permissions(dir / "locked", fs::perms::owner_read | fs::perms::owner_exec);
    std::error_code error;
    const auto start = std::chrono::steady_clock::now();
    CHECK(!remove_temp_dir(dir, 300ms, error) && error);
    CHECK(std::chrono::steady_clock::now() - start < 5s);
    fs::permissions(dir / "locked", fs::perms::owner_all);
    CHECK(remove_temp_dir(dir, 1s, error) && !error);
  }
#endif
  if (failures == 0) {
    std::puts("host.temp-dir: all checks passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
