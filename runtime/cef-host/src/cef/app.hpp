// The CefApp for every process: scheme registration everywhere, window creation in the browser
// process, the bridge in renderers.
#pragma once

#include <memory>

#include "bridge.hpp"
#include "include/cef_app.h"

namespace pico_desktop::host::cef {

class HostApp : public CefApp, public CefBrowserProcessHandler, public CefRenderProcessHandler {
 public:
  /// `browser`: whether this is the browser process (it has the host's options).
  explicit HostApp(bool browser);

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;

  // Browser process.
  void OnContextInitialized() override;
  void OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line) override;
  bool OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine> command_line,
                                   const CefString& current_directory) override;

  // Renderer processes.
  void OnWebKitInitialized() override;
  void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;
  void OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                CefProcessId source_process, CefRefPtr<CefProcessMessage> message) override;

 private:
  bool browser_;
  std::unique_ptr<RendererBridge> bridge_;

  IMPLEMENT_REFCOUNTING(HostApp);
};

}  // namespace pico_desktop::host::cef
