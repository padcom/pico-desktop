#include "client.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <cstdio>
#include <string>
#include <vector>

#include "host.hpp"
#include "link.hpp"
#include "include/cef_app.h"
#include "include/cef_process_message.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "pico_desktop/host/policy.hpp"
#include "pico_desktop/host/services.hpp"
#include "pico_desktop/host/sandbox.hpp"

namespace pico_desktop::host::cef {

namespace {

constexpr std::string_view kTestPrefix = "pd-test: ";

}  // namespace

bool HostClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                          CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  const std::string name = message->GetName().ToString();
  if (source_process != PID_RENDERER || (name != kBridgeSend && name != kBridgeClosed && name != kBridgeAck)) {
    return false;
  }
  CefRefPtr<CefListValue> in = message->GetArgumentList();
  if (!frame || in->GetType(0) != VTYPE_INT) {
    return true;
  }
  if (name == kBridgeAck) {
    if (in->GetType(1) == VTYPE_INT && in->GetInt(1) > 0) {
      link().page_acked(frame->GetIdentifier().ToString(), in->GetInt(0), static_cast<std::uint64_t>(in->GetInt(1)));
    }
    return true;
  }
  if (name == kBridgeClosed) {
    link().page_closed(frame->GetIdentifier().ToString(), in->GetInt(0));
    return true;
  }
  // Check again here: the renderer is the less trusted side.
  const std::string url = frame->GetURL().ToString();
  if (!bridge_allowed(browser_state().options.policy, url, frame->IsMain())) {
    log_line("pd-host: rejected a bridge message from an untrusted frame (" + url + ")");
    return true;
  }
  CefRefPtr<CefBinaryValue> bytes = in->GetType(1) == VTYPE_BINARY ? in->GetBinary(1) : nullptr;
  if (!bytes || bytes->GetSize() == 0 || bytes->GetSize() > kMaxBridgeMessage) {
    log_line("pd-host: dropped a bridge message that isn't 1 byte to 4 MiB");
    return true;
  }
  std::vector<std::byte> data(bytes->GetSize());
  bytes->GetData(data.data(), data.size(), 0);
  link().from_page(browser, frame, in->GetInt(0), data);
  return true;
}

void HostClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus status, int error_code,
                                           const CefString& error_string) {
  CEF_REQUIRE_UI_THREAD();
  log_line("pd-host: the renderer ended (status " + std::to_string(status) + ")");
  link().browser_gone(browser->GetIdentifier());
}

void HostClient::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
  // Without a fixed --window-title the window shows the page's <title>.
  if (!browser_state().options.window_title.empty()) {
    return;
  }
  CefRefPtr<CefBrowserView> view = CefBrowserView::GetForBrowser(browser);
  if (CefRefPtr<CefWindow> window = view ? view->GetWindow() : nullptr) {
    window->SetTitle(title);
  }
}

bool HostClient::OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString& message,
                                  const CefString& source, int line) {
  BrowserState& state = browser_state();
  const std::string text = message.ToString();
  if (state.options.mode != Mode::self_test) {
    if (state.options.dev) {
      log_line("console: " + text);  // development: the page's console in the terminal too
      return true;
    }
    return false;  // the default handling logs it
  }
  if (!text.starts_with(kTestPrefix)) {
    log_line("console: " + text);
    return true;
  }
  log_line(text);
  const std::string_view result = std::string_view(text).substr(kTestPrefix.size());
  if (result.starts_with("ok ")) {
    ++state.self_test_passed;
  } else if (result.starts_with("fail ")) {
    ++state.self_test_failed;
  } else if (result == "done") {
    finish_self_test(browser);
  }
  return true;
}

#if defined(_WIN32)
namespace {
/// An integrity level as Windows writes it (0x1000), or "?".
std::string integrity_text(long level) {
  if (level < 0) {
    return "?";
  }
  char text[16];
  std::snprintf(text, sizeof text, "0x%04lX", static_cast<unsigned long>(level));
  return text;
}
}  // namespace
#endif

void HostClient::finish_self_test(CefRefPtr<CefBrowser> browser) {
  BrowserState& state = browser_state();
  if (state.self_test_done) {
    return;
  }
  // The page passed its checks; now prove the renderer it ran in is sandboxed.
#if defined(__APPLE__)
  // macOS: the helpers run under Seatbelt (sandbox_check, as Chromium's own sandbox tests ask).
  const auto processes = inspect_descendants_mac(static_cast<int>(::getpid()));
  for (const auto& p : processes) {
    log_line("pd-sandbox: " + p.type + " pid=" + std::to_string(p.pid) + " seatbelt=" + std::to_string(p.seatbelt));
  }
  if (mac_renderers_sandboxed(processes)) {
    log_line("pd-test: ok renderer is sandboxed (Seatbelt)");
    ++state.self_test_passed;
  } else {
    log_line("pd-test: fail renderer is sandboxed");
    ++state.self_test_failed;
  }
#elif defined(_WIN32)
  // Windows: restricted tokens below Medium integrity, in job objects (set up by the bootstrap).
  const auto processes = inspect_descendants_windows(GetCurrentProcessId());
  for (const auto& p : processes) {
    log_line("pd-sandbox: " + p.type + " pid=" + std::to_string(p.pid) + " integrity=" + integrity_text(p.integrity) +
             " job=" + (p.in_job ? "yes" : "no"));
  }
  if (windows_renderers_sandboxed(processes)) {
    log_line("pd-test: ok renderer is sandboxed (below Medium integrity, in a job object)");
    ++state.self_test_passed;
  } else {
    log_line("pd-test: fail renderer is sandboxed");
    ++state.self_test_failed;
  }
#else
  const auto processes = inspect_descendants(static_cast<int>(::getpid()));
  for (const auto& p : processes) {
    log_line("pd-sandbox: " + p.type + " pid=" + std::to_string(p.pid) + " seccomp=" +
             std::to_string(p.seccomp_mode) + " own-pid-namespace=" + (p.own_pid_namespace ? "yes" : "no"));
  }
  if (renderers_sandboxed(processes)) {
    log_line("pd-test: ok renderer is sandboxed (seccomp-bpf, own PID namespace)");
    ++state.self_test_passed;
  } else {
    log_line("pd-test: fail renderer is sandboxed");
    ++state.self_test_failed;
  }
#endif
  state.self_test_done = true;
  log_line("pd-self-test: " + std::to_string(state.self_test_passed) + " passed, " +
           std::to_string(state.self_test_failed) + " failed");
  browser->GetHost()->CloseBrowser(true);
}

bool HostClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event, CefEventHandle os_event,
                               bool* is_keyboard_shortcut) {
  constexpr int kF12 = 0x7B;
  if (browser_state().options.dev && event.type == KEYEVENT_RAWKEYDOWN && event.windows_key_code == kF12) {
    CefWindowInfo info;
    CefBrowserSettings settings;
    browser->GetHost()->ShowDevTools(info, nullptr, settings, CefPoint());
    return true;
  }
  return false;
}

bool HostClient::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id,
                               const CefString& target_url, const CefString& target_frame_name,
                               CefLifeSpanHandler::WindowOpenDisposition target_disposition, bool user_gesture,
                               const CefPopupFeatures& popup_features, CefWindowInfo& window_info,
                               CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
                               CefRefPtr<CefDictionaryValue>& extra_info, bool* no_javascript_access) {
  // v1 has one window: popups never open in the app.
  leave_app(target_url.ToString(), user_gesture, "popup");
  return true;
}

void HostClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  BrowserState& state = browser_state();
  ++state.open_browsers;
  if (!state.browser) {
    state.browser = browser;
  }
}

void HostClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  BrowserState& state = browser_state();
  if (state.browser && state.browser->IsSame(browser)) {
    state.browser = nullptr;
  }
  link().browser_gone(browser->GetIdentifier());
  if (--state.open_browsers == 0) {
    link().closing();
    CefQuitMessageLoop();
  }
}

void HostClient::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode error_code,
                             const CefString& error_text, const CefString& failed_url) {
  if (error_code == ERR_ABORTED) {
    return;  // a navigation we cancelled
  }
  const std::string what = "load error " + std::to_string(error_code) + " (" + error_text.ToString() + ") for " +
                           failed_url.ToString();
  log_line("pd-host: " + what);
  if (browser_state().options.mode == Mode::self_test && frame->IsMain()) {
    fail_self_test(browser, what);
  }
}

bool HostClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefRequest> request, bool user_gesture, bool is_redirect) {
  const std::string url = request->GetURL().ToString();
  const Navigation decision = decide_navigation(browser_state().options.policy, url);
  if (decision == Navigation::allow) {
    return false;
  }
  // Subframes never open anything externally; they just don't load.
  leave_app(url, user_gesture && frame->IsMain(), frame->IsMain() ? "navigation" : "subframe navigation");
  return true;
}

bool HostClient::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  const CefString& target_url, CefRequestHandler::WindowOpenDisposition target_disposition,
                                  bool user_gesture) {
  leave_app(target_url.ToString(), user_gesture, "new-tab navigation");
  return true;
}

void HostClient::leave_app(const std::string& url, bool user_gesture, const char* how) {
  const Navigation decision = decide_navigation(browser_state().options.policy, url);
  if (decision == Navigation::open_external && user_gesture) {
    open_external(url);
    return;
  }
  log_line(std::string("pd-host: blocked ") + how + " to " + url +
           (decision == Navigation::open_external ? " (no user gesture)" : " (not a web link)"));
}

void fail_self_test(CefRefPtr<CefBrowser> browser, const std::string& reason) {
  BrowserState& state = browser_state();
  if (state.self_test_done) {
    return;
  }
  log_line("pd-test: fail " + reason);
  ++state.self_test_failed;
  state.self_test_done = true;
  log_line("pd-self-test: " + std::to_string(state.self_test_passed) + " passed, " +
           std::to_string(state.self_test_failed) + " failed");
  if (browser) {
    browser->GetHost()->CloseBrowser(true);
  } else {
    CefQuitMessageLoop();
  }
}

bool HostClient::OnShowPermissionPrompt(CefRefPtr<CefBrowser> /*browser*/, uint64_t /*prompt_id*/,
                                        const CefString& requesting_origin, uint32_t requested_permissions,
                                        CefRefPtr<CefPermissionPromptCallback> callback) {
  const Options& options = browser_state().options;
  const bool clipboard_only = requested_permissions == CEF_PERMISSION_TYPE_CLIPBOARD;
  const bool granted = permission_granted(options.policy, requesting_origin.ToString(), clipboard_only,
                                          options.capabilities.contains(Capability::clipboard));
  if (!granted) {
    log_line("pd-host: denied a permission request from " + requesting_origin.ToString() + " (types " +
             std::to_string(requested_permissions) + ")");
  }
  callback->Continue(granted ? CEF_PERMISSION_RESULT_ACCEPT : CEF_PERMISSION_RESULT_DENY);
  return true;
}

}  // namespace pico_desktop::host::cef
