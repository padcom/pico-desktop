#include "app.hpp"

#include "host.hpp"
#include "link.hpp"
#include "include/cef_command_line.h"
#include "include/cef_scheme.h"
#include "scheme.hpp"

namespace pico_desktop::host::cef {

HostApp::HostApp(bool browser) : browser_(browser) {}

void HostApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  // Every process registers the same schemes with the same options. `app:` is a standard,
  // secure scheme: real origins, secure context, CORS and fetch() like https (ADR-0003).
  registrar->AddCustomScheme("app", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                                        CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_FETCH_ENABLED);
}

void HostApp::OnContextInitialized() {
  register_asset_scheme(browser_state().options);
  link().start();
  create_app_window();
}

void HostApp::OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line) {
  const Policy& policy = browser_state().options.policy;
  command_line->AppendSwitchWithValue(kSwitchAppId, policy.app_id);
  command_line->AppendSwitchWithValue(kSwitchOrigin, policy.scheme == OriginScheme::https ? "https" : "app");
  if (policy.dev_origin) {
    command_line->AppendSwitchWithValue(kSwitchDevOrigin, *policy.dev_origin);
  }
  command_line->AppendSwitchWithValue(kSwitchBridgeSubframes, policy.bridge_in_subframes ? "1" : "0");
}

bool HostApp::OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine> command_line,
                                          const CefString& current_directory) {
  // Single instance: a second launch of this app (the same profile) brings this window to the
  // front. Handled here, so CEF never opens a window of its own.
  log_line("pd-host: launched again: bringing the window to the front");
  focus_app_window();
  return true;
}

void HostApp::OnWebKitInitialized() {
  bridge_ = std::make_unique<RendererBridge>();
}

void HostApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefV8Context> context) {
  if (bridge_) {
    bridge_->on_context_created(frame, context);
  }
}

void HostApp::OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefV8Context> context) {
  if (bridge_) {
    bridge_->on_context_released(frame, context);
  }
}

bool HostApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                       CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  return bridge_ && source_process == PID_BROWSER && bridge_->on_reply(frame, message);
}

}  // namespace pico_desktop::host::cef
