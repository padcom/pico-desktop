// Development: the dev CLI drives the host through its stdin (`--control=stdin`): one command per
// line (restart-backend, reload, quit). End of input quits, so the host never outlives the CLI.
#pragma once

namespace pico_desktop::host::cef {

/// Starts reading commands (browser process, after CEF is initialized).
void start_control_reader();

}  // namespace pico_desktop::host::cef
