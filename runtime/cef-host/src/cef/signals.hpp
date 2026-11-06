// SIGTERM, SIGINT and SIGHUP (a logout, a terminal's Ctrl+C, `kill`) close the app the orderly
// way, like its close button: the page and the backend get `closing` first. A second one quits
// at once.
#pragma once

namespace pico_desktop::host::cef {

/// Starts handling them (browser process, after CEF is initialized).
void start_signal_watch();

}  // namespace pico_desktop::host::cef
