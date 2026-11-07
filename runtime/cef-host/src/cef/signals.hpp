// SIGTERM, SIGINT and SIGHUP (a logout, a terminal's Ctrl+C, `kill`) close the app the orderly
// way, like its close button: the page and the backend get `closing` first. A second one quits
// at once. Windows: Ctrl+C, Ctrl+Break, and the console closing, logoff and shutdown, likewise.
#pragma once

namespace pico_desktop::host::cef {

/// Starts handling them (browser process, after CEF is initialized).
void start_signal_watch();

/// The orderly shutdown is over (browser process, last thing before returning). Windows: a
/// console close, logoff or shutdown handler waits for this, since the system ends the process
/// once the handler returns (#18). POSIX: nothing.
void signal_watch_shutdown_done();

}  // namespace pico_desktop::host::cef
