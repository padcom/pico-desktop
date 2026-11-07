#include "pico_desktop/host/policy.hpp"

#include <array>

namespace pico_desktop::host {

namespace {

bool starts_with_origin(std::string_view url, std::string_view origin) {
  if (!url.starts_with(origin)) {
    return false;
  }
  // `app://hello` must not match `app://hello.evil/` or `app://hello:8080/`.
  return url.size() == origin.size() || url[origin.size()] == '/' || url[origin.size()] == '?' ||
         url[origin.size()] == '#';
}

std::string_view scheme_of(std::string_view url) {
  const auto colon = url.find(':');
  return colon == std::string_view::npos ? std::string_view{} : url.substr(0, colon);
}

}  // namespace

std::string trusted_host(const Policy& policy) {
  return policy.scheme == OriginScheme::app ? policy.app_id : policy.app_id + ".localhost";
}

std::string trusted_origin(const Policy& policy) {
  return (policy.scheme == OriginScheme::app ? "app://" : "https://") + trusted_host(policy);
}

std::string start_url(const Policy& policy) {
  return policy.dev_origin ? *policy.dev_origin + "/" : trusted_origin(policy) + "/index.html";
}

bool is_trusted_url(const Policy& policy, std::string_view url) {
  return starts_with_origin(url, trusted_origin(policy)) ||
         (policy.dev_origin && starts_with_origin(url, *policy.dev_origin));
}

Navigation decide_navigation(const Policy& policy, std::string_view url) {
  if (is_trusted_url(policy, url)) {
    return Navigation::allow;
  }
  static constexpr std::array<std::string_view, 3> external{"http", "https", "mailto"};
  for (const auto scheme : external) {
    if (scheme_of(url) == scheme) {
      return Navigation::open_external;
    }
  }
  return Navigation::block;
}

bool bridge_allowed(const Policy& policy, std::string_view frame_url, bool is_main_frame) {
  return is_trusted_url(policy, frame_url) && (is_main_frame || policy.bridge_in_subframes);
}

bool is_valid_app_id(std::string_view id) {
  if (id.empty() || id.size() > 200) {
    return false;
  }
  std::size_t label = 0;
  char previous = '.';
  for (const char c : id) {
    if (c == '.') {
      if (label == 0 || previous == '-') {
        return false;
      }
      label = 0;
    } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-' && label > 0)) {
      if (++label > 63) {
        return false;
      }
    } else {
      return false;
    }
    previous = c;
  }
  return label > 0 && previous != '-';
}

bool permission_granted(const Policy& policy, std::string_view origin, bool clipboard_only, bool clipboard_declared) {
  // The prompt's origin has no path: give it one, as a URL on that origin.
  return clipboard_only && clipboard_declared && is_trusted_url(policy, std::string(origin) + "/");
}

}  // namespace pico_desktop::host
