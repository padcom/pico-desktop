// Serving the app's bundled assets on the trusted origin.
#pragma once

#include "pico_desktop/host/options.hpp"

namespace pico_desktop::host::cef {

/// Registers the handler for the trusted origin (`app://<id>/` or `https://<id>.localhost/`) that
/// serves `options.app_dir` (browser process, after CEF is initialized). Nothing with --dev-url.
void register_asset_scheme(const Options& options);

}  // namespace pico_desktop::host::cef
