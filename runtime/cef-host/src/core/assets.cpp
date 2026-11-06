#include "pico_desktop/host/assets.hpp"

#include <array>
#include <cctype>
#include <system_error>
#include <utility>

namespace pico_desktop::host {

namespace fs = std::filesystem;

namespace {

int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

/// Whether `inner` is `outer` or inside it (both canonical).
bool is_within(const fs::path& outer, const fs::path& inner) {
  auto o = outer.begin();
  auto i = inner.begin();
  for (; o != outer.end(); ++o, ++i) {
    if (i == inner.end() || *o != *i) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::optional<std::string> percent_decode(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c == '%') {
      if (i + 2 >= text.size()) {  // `%` needs two hex digits after it
        return std::nullopt;
      }
      const int hi = hex_value(text[i + 1]);
      const int lo = hex_value(text[i + 2]);
      if (hi < 0 || lo < 0) {
        return std::nullopt;
      }
      c = static_cast<char>(hi * 16 + lo);
      i += 2;
    }
    if (c == '\0') {
      return std::nullopt;
    }
    out.push_back(c);
  }
  return out;
}

std::optional<fs::path> resolve_asset(const fs::path& root, std::string_view url_path) {
  // Drop the query and fragment, if the caller passed them along.
  if (const auto cut = url_path.find_first_of("?#"); cut != std::string_view::npos) {
    url_path = url_path.substr(0, cut);
  }
  if (url_path.empty() || url_path.front() != '/') {
    return std::nullopt;
  }
  // Split on the *encoded* path's slashes, so `%2F` can't introduce a separator, then decode each
  // segment and reject the ones that could climb or separate.
  fs::path relative;
  std::size_t start = 1;
  while (start <= url_path.size()) {
    const auto end = std::min(url_path.find('/', start), url_path.size());
    const auto decoded = percent_decode(url_path.substr(start, end - start));
    if (!decoded) {
      return std::nullopt;
    }
    const std::string& segment = *decoded;
    if (segment == ".." || segment.find('/') != std::string::npos || segment.find('\\') != std::string::npos) {
      return std::nullopt;
    }
    if (!segment.empty() && segment != ".") {
      relative /= segment;
    }
    start = end + 1;
  }
  const bool wants_index = url_path.back() == '/' || relative.empty();

  std::error_code error;
  const fs::path canonical_root = fs::canonical(root, error);
  if (error) {
    return std::nullopt;
  }
  fs::path candidate = canonical_root / relative;
  if (wants_index || fs::is_directory(candidate, error)) {
    candidate /= "index.html";
  }
  // Resolve symlinks and require the result to stay inside the root.
  const fs::path resolved = fs::canonical(candidate, error);
  if (error || !is_within(canonical_root, resolved) || !fs::is_regular_file(resolved, error)) {
    return std::nullopt;
  }
  return resolved;
}

std::string mime_type_for(const fs::path& file) {
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 20> types{{
    {".html", "text/html"},
    {".htm", "text/html"},
    {".js", "text/javascript"},
    {".mjs", "text/javascript"},
    {".css", "text/css"},
    {".json", "application/json"},
    {".map", "application/json"},
    {".svg", "image/svg+xml"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".webp", "image/webp"},
    {".ico", "image/x-icon"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".wasm", "application/wasm"},
    {".txt", "text/plain"},
    {".webmanifest", "application/manifest+json"},
    {".xml", "application/xml"},
  }};
  std::string ext = file.extension().string();
  for (auto& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  for (const auto& [suffix, type] : types) {
    if (ext == suffix) {
      return std::string(type);
    }
  }
  return "application/octet-stream";
}

}  // namespace pico_desktop::host
