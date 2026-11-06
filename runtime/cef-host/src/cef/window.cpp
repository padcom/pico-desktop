// The app window: a CEF Views window holding one browser view, both in the Alloy runtime style
// (no Chrome UI or Chrome shortcuts in an app window).
#include <string>

#include "client.hpp"
#include "host.hpp"
#include "include/base/cef_callback.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace pico_desktop::host::cef {

namespace {

constexpr int kSelfTestTimeoutMs = 60'000;

class BrowserViewDelegate : public CefBrowserViewDelegate {
 public:
  cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }

 private:
  IMPLEMENT_REFCOUNTING(BrowserViewDelegate);
};

class WindowDelegate : public CefWindowDelegate {
 public:
  explicit WindowDelegate(CefRefPtr<CefBrowserView> view) : view_(view) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->AddChildView(view_);
    window->Show();
    view_->RequestFocus();
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override { view_ = nullptr; }

  bool CanClose(CefRefPtr<CefWindow> window) override {
    // Close the browser first (it runs the page's unload handlers); the window follows.
    CefRefPtr<CefBrowser> browser = view_ ? view_->GetBrowser() : nullptr;
    return !browser || browser->GetHost()->TryCloseBrowser();
  }

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override { return CefSize(1024, 768); }

  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }

 private:
  CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(WindowDelegate);
};

void self_test_timeout() {
  fail_self_test(browser_state().browser, "timeout: the page didn't report `pd-test: done`");
}

}  // namespace

void create_app_window() {
  CEF_REQUIRE_UI_THREAD();
  const Options& options = browser_state().options;
  CefBrowserSettings settings;
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      new HostClient(), start_url(options.policy), settings, nullptr, nullptr, new BrowserViewDelegate());
  CefWindow::CreateTopLevelWindow(new WindowDelegate(view));
  if (options.mode == Mode::self_test) {
    CefPostDelayedTask(TID_UI, base::BindOnce(&self_test_timeout), kSelfTestTimeoutMs);
  }
}

}  // namespace pico_desktop::host::cef
