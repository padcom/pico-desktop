// A fresh directory in the system's temporary directory that only this user can use (the
// self-test's throwaway profile).
#pragma once

#include <filesystem>
#include <string>

namespace pico_desktop::host {

/// Creates `<temp>/<prefix><unique>` and returns it, or an empty path when that fails. POSIX:
/// with mkdtemp, so the directory is 0700 and its name is random (the temporary directory is
/// shared by every user). Windows: the user's own %TEMP%, so a unique name is enough.
std::filesystem::path make_private_temp_dir(const std::string& prefix);

}  // namespace pico_desktop::host
