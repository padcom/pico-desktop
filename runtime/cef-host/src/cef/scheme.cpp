#include "scheme.hpp"

#include <string>

#include "host.hpp"
#include "include/cef_parser.h"
#include "include/cef_scheme.h"
#include "include/cef_stream.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "pico_desktop/host/assets.hpp"

namespace pico_desktop::host::cef {

namespace {

// The production CSP for bundled pages: everything from the app's own origin, no inline script,
// no plugins, not embeddable elsewhere.
constexpr const char* kContentSecurityPolicy =
    "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data: blob:; "
    "font-src 'self'; connect-src 'self'; frame-src 'self'; object-src 'none'; base-uri 'self'; "
    "form-action 'self'; frame-ancestors 'self'";

/// A short text response. `body` must be a string literal: the reader points at it.
CefRefPtr<CefResourceHandler> text_response(int status, const char* status_text, const char* body) {
  CefResponse::HeaderMap headers;
  headers.insert({"X-Content-Type-Options", "nosniff"});
  headers.insert({"Cache-Control", "no-store"});
  return new CefStreamResourceHandler(status, status_text, "text/plain", headers,
                                      CefStreamReader::CreateForData(const_cast<char*>(body), std::char_traits<char>::length(body)));
}

class AssetHandlerFactory : public CefSchemeHandlerFactory {
 public:
  explicit AssetHandlerFactory(std::filesystem::path root) : root_(std::move(root)) {}

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name, CefRefPtr<CefRequest> request) override {
    const std::string method = request->GetMethod().ToString();
    if (method != "GET" && method != "HEAD") {
      return text_response(405, "Method Not Allowed", "method not allowed\n");
    }
    CefURLParts parts;
    if (!CefParseURL(request->GetURL(), parts)) {
      return text_response(400, "Bad Request", "bad request\n");
    }
    const auto file = resolve_asset(root_, CefString(&parts.path).ToString());
    if (!file) {
      return text_response(404, "Not Found", "not found\n");
    }
    CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForFile(file->string());
    if (!stream) {
      return text_response(404, "Not Found", "not found\n");
    }
    const std::string mime = mime_type_for(*file);
    CefResponse::HeaderMap headers;
    headers.insert({"X-Content-Type-Options", "nosniff"});
    headers.insert({"Cache-Control", "no-cache"});
    if (mime == "text/html") {
      headers.insert({"Content-Security-Policy", kContentSecurityPolicy});
    }
    return new CefStreamResourceHandler(200, "OK", mime, headers, stream);
  }

 private:
  std::filesystem::path root_;
  IMPLEMENT_REFCOUNTING(AssetHandlerFactory);
};

}  // namespace

void register_asset_scheme(const Options& options) {
  if (options.policy.dev_origin) {
    return;  // the dev server serves the frontend
  }
  const char* scheme = options.policy.scheme == OriginScheme::https ? "https" : "app";
  CefRegisterSchemeHandlerFactory(scheme, trusted_host(options.policy), new AssetHandlerFactory(options.app_dir));
}

}  // namespace pico_desktop::host::cef
