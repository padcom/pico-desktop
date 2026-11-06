// The app browser's CefClient: navigation and popup policy, the browser side of the bridge,
// self-test reporting, DevTools in development, and closing.
#pragma once

#include "include/cef_client.h"

namespace pico_desktop::host::cef {

class HostClient : public CefClient,
                   public CefDisplayHandler,
                   public CefKeyboardHandler,
                   public CefLifeSpanHandler,
                   public CefLoadHandler,
                   public CefRequestHandler {
 public:
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                CefProcessId source_process, CefRefPtr<CefProcessMessage> message) override;

  // CefDisplayHandler
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString& message,
                        const CefString& source, int line) override;

  // CefKeyboardHandler
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event, CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;

  // CefLifeSpanHandler
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id,
                     const CefString& target_url, const CefString& target_frame_name,
                     CefLifeSpanHandler::WindowOpenDisposition target_disposition, bool user_gesture,
                     const CefPopupFeatures& popup_features, CefWindowInfo& window_info,
                     CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info, bool* no_javascript_access) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefLoadHandler
  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode error_code,
                   const CefString& error_text, const CefString& failed_url) override;

  // CefRequestHandler
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
                      bool user_gesture, bool is_redirect) override;
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& target_url,
                        CefRequestHandler::WindowOpenDisposition target_disposition, bool user_gesture) override;

 private:
  /// Leaving the trusted origin: open web links externally (only on a user gesture), block the rest.
  void leave_app(const std::string& url, bool user_gesture, const char* how);
  void finish_self_test(CefRefPtr<CefBrowser> browser);

  IMPLEMENT_REFCOUNTING(HostClient);
};

}  // namespace pico_desktop::host::cef
