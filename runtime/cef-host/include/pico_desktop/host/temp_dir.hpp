// A fresh directory in the system's temporary directory that only this user can use (the
// self-test's throwaway profile).
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace pico_desktop::host {

/// Creates `<temp>/<prefix><unique>` and returns it, or an empty path when that fails. POSIX:
/// with mkdtemp, so the directory is 0700 and its name is random (the temporary directory is
/// shared by every user). Windows: the user's own %TEMP%, so a unique name is enough.
std::filesystem::path make_private_temp_dir(const std::string& prefix);

/// Removes `dir` and everything in it, retrying for up to `budget` while that fails: on Windows,
/// Chromium's subprocesses may still hold files in a profile for a moment after CefShutdown
/// (#23). Each pass removes all it can, past a file that's held, so only held files (and the
/// directories holding them) are left. True once `dir` is gone (or never existed); otherwise
/// `error` is the first failure of the last pass.
bool remove_temp_dir(const std::filesystem::path& dir, std::chrono::milliseconds budget, std::error_code& error);

}  // namespace pico_desktop::host
