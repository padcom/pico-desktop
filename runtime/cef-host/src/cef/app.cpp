#include "app.hpp"

#include "host.hpp"
#include "link.hpp"
#include "include/cef_command_line.h"
#include "include/cef_scheme.h"
#include "scheme.hpp"

namespace pico_desktop::host::cef {

void HostApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  // Every process registers the same schemes with the same options. `app:` is a standard,
  // secure scheme: real origins, secure context, CORS and fetch() like https (ADR-0003).
  registrar->AddCustomScheme("app", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                                        CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_FETCH_ENABLED);
}

void HostApp::OnBeforeCommandLineProcessing(const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
  (void)process_type;
  (void)command_line;
#if defined(__linux__)
  // Without a GPU (a VM, a container, CI under Xvfb) Chromium no longer falls back to its software
  // renderer for WebGL by itself: no WebGL context could be created in CI (docs/graphics.md). This
  // allows the fallback. The exposure: shaders the page supplies reach SwiftShader's JIT compiler in
  // the GPU process (whose Linux sandbox is the weaker kind), and exploiting it needs script running
  // in the app's page first. That is acceptable while the host shows only the app's own content (CSP
  // `script-src 'self'`, no navigation elsewhere; ADR 0003). It applies to every app on Linux and to
  // every mode, including `dev` (Vite's origin has no CSP), and it must be re-examined if the host
  // ever shows content the app does not control. `--no-software-webgl` turns it off. Set on the
  // browser process; Chromium passes it on to the GPU process.
  if (process_type.empty() && browser_state().options.software_webgl) {
    command_line->AppendSwitch("enable-unsafe-swiftshader");
  }
#endif
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
