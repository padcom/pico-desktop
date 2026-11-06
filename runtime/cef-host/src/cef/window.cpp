// The app window: a CEF Views window holding one browser view, both in the Alloy runtime style
// (no Chrome UI or Chrome shortcuts in an app window).
#include <string>

#include "client.hpp"
#include "host.hpp"
#include "include/cef_app.h"
#include "link.hpp"
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

class WindowDelegate;

void shutdown_done(CefRefPtr<WindowDelegate> delegate, CefRefPtr<CefWindow> window);

class WindowDelegate : public CefWindowDelegate {
 public:
  explicit WindowDelegate(CefRefPtr<CefBrowserView> view) : view_(view) {}

  /// The backend is gone: close for real.
  void shutdown_complete(CefRefPtr<CefWindow> window) {
    shutdown_complete_ = true;
    window->Close();
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->AddChildView(view_);
    window->Show();
    view_->RequestFocus();
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override { view_ = nullptr; }

  bool CanClose(CefRefPtr<CefWindow> window) override {
    // First an orderly shutdown while the page is still there: it and the backend get `closing`,
    // and the window closes once the backend has finished (or was stopped after a grace period).
    if (!shutdown_complete_) {
      if (!shutdown_started_) {
        shutdown_started_ = true;
        link().begin_close(base::BindOnce(&shutdown_done, CefRefPtr<WindowDelegate>(this), window));
      }
      return false;
    }
    // Then the browser (it runs the page's unload handlers); the window follows.
    CefRefPtr<CefBrowser> browser = view_ ? view_->GetBrowser() : nullptr;
    return !browser || browser->GetHost()->TryCloseBrowser();
  }

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override { return CefSize(1024, 768); }

  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }

 private:
  CefRefPtr<CefBrowserView> view_;
  bool shutdown_started_ = false;
  bool shutdown_complete_ = false;
  IMPLEMENT_REFCOUNTING(WindowDelegate);
};

void shutdown_done(CefRefPtr<WindowDelegate> delegate, CefRefPtr<CefWindow> window) {
  delegate->shutdown_complete(window);
}

void self_test_timeout() {
  fail_self_test(browser_state().browser, "timeout: the page didn't report `pd-test: done`");
}

}  // namespace

void close_app_window() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowser> browser = browser_state().browser;
  CefRefPtr<CefBrowserView> view = browser ? CefBrowserView::GetForBrowser(browser) : nullptr;
  CefRefPtr<CefWindow> window = view ? view->GetWindow() : nullptr;
  if (window) {
    window->Close();  // through CanClose: the orderly shutdown
  } else {
    CefQuitMessageLoop();
  }
}

void focus_app_window() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowser> browser = browser_state().browser;
  CefRefPtr<CefBrowserView> view = browser ? CefBrowserView::GetForBrowser(browser) : nullptr;
  CefRefPtr<CefWindow> window = view ? view->GetWindow() : nullptr;
  if (!window) {
    return;
  }
  if (window->IsMinimized()) {
    window->Restore();
  }
  window->Show();
  window->Activate();
}

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
