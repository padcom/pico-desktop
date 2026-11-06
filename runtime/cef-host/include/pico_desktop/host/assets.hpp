// Serving the app's bundled assets: mapping a request path under the trusted origin to a file
// inside the asset root, never outside it (ADR-0003). No CEF types, so it's unit-tested alone.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace pico_desktop::host {

/// The file for `url_path` (the path part of a trusted-origin URL, still percent-encoded, with or
/// without a query or fragment) inside `root`, or nothing when it would leave `root`, names a
/// directory without an index, or doesn't exist. `/` and paths ending in `/` serve `index.html`.
/// Rejected: `..` segments (also percent-encoded), encoded `/` or `\`, NUL bytes, malformed
/// escapes, and symlinks that resolve outside `root`.
std::optional<std::filesystem::path> resolve_asset(const std::filesystem::path& root, std::string_view url_path);

/// The MIME type for a file name, by extension (`application/octet-stream` when unknown).
std::string mime_type_for(const std::filesystem::path& file);

/// Percent-decodes `text`; nothing when an escape is malformed or decodes to NUL.
std::optional<std::string> percent_decode(std::string_view text);

}  // namespace pico_desktop::host
