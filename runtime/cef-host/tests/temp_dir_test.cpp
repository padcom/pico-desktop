// Unit test for make_private_temp_dir (the self-test's throwaway profile, #27): a new directory
// in the temporary directory, and on POSIX only its owner's (0700) with a name nobody can guess.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "pico_desktop/host/temp_dir.hpp"

namespace fs = std::filesystem;
using pico_desktop::host::make_private_temp_dir;

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
  if (failures == 0) {
    std::puts("host.temp-dir: all checks passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
