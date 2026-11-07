// A fresh directory in the system's temporary directory that only this user can use (the
// self-test's throwaway profile).
#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
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

/// Whether a process with this id may be running: false only when the system says there's no
/// such process (so a live instance's profile can never be taken for an ended one's).
bool process_alive(long pid);

/// Removes the subdirectories of `instances` named after a process id that `alive` says has
/// ended, other than `own_pid`'s: the profiles of earlier --multi-instance runs. On Windows a
/// process can't remove its own profile (Chromium holds files in it until the process exits,
/// #23), so the next run does. Anything else there is left alone. Returns how many it removed.
/// Known limit: hosts in different PID namespaces sharing one cache directory (Linux containers,
/// toolbox or flatpak setups sharing $XDG_CACHE_HOME) can't see each other's processes, so one
/// can take the other's live profile for an ended one's and remove it.
int remove_ended_instances(const std::filesystem::path& instances, long own_pid,
                           const std::function<bool(long)>& alive);

}  // namespace pico_desktop::host
