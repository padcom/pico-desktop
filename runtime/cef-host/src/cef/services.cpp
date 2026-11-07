#include "services.hpp"

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>
#endif

#include <cerrno>
#include <thread>
#include <vector>

#include "clipboard.hpp"
#include "host.hpp"
#include "include/cef_task.h"
#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "link.hpp"
#include "pico_desktop/ipc/cbor.hpp"

#if !defined(_WIN32)
extern char** environ;
#endif

namespace pico_desktop::host::cef {

namespace {

using Bytes = std::vector<std::byte>;

CefRefPtr<CefWindow> app_window() {
  CefRefPtr<CefBrowser> browser = browser_state().browser;
  CefRefPtr<CefBrowserView> view = browser ? CefBrowserView::GetForBrowser(browser) : nullptr;
  return view ? view->GetWindow() : nullptr;
}

std::string text_or(const ipc::Value& params, const char* key, const std::string& fallback = {}) {
  const ipc::Value* v = params.get(key);
  return v && v->as_text() ? *v->as_text() : fallback;
}

void ok(const ServiceCall& call) { link().reply(call, std::nullopt); }

// ------------------------------------------------------------------------------------- window

void window_service(const ServiceCall& call, const std::string& name, const ipc::Value& params) {
  CefRefPtr<CefWindow> window = app_window();
  if (!window) {
    link().refuse(call, "UNAVAILABLE", "there's no window");
    return;
  }
  if (name == "host.window.setTitle") {
    const ipc::Value* title = params.get("title");
    if (!title || !title->as_text()) {
      link().refuse(call, "INVALID_PAYLOAD", "$.title: expected text");
      return;
    }
    window->SetTitle(*title->as_text());
    ok(call);
  } else if (name == "host.window.getTitle") {
    link().reply(call, ipc::Writer().map(1).text("title").text(window->GetTitle().ToString()).take());
  } else if (name == "host.window.minimize") {
    window->Minimize();
    ok(call);
  } else if (name == "host.window.maximize") {
    window->Maximize();
    ok(call);
  } else if (name == "host.window.restore") {
    window->Restore();
    ok(call);
  } else if (name == "host.window.focus") {
    focus_app_window();
    ok(call);
  } else if (name == "host.window.setSize") {
    const ipc::Value* w = params.get("width");
    const ipc::Value* h = params.get("height");
    if (!w || !h || !w->as_uint() || !h->as_uint() || *w->as_uint() > 32768 || *h->as_uint() > 32768) {
      link().refuse(call, "INVALID_PAYLOAD", "$: expected { width, height } in pixels");
      return;
    }
    window->SetSize(CefSize(static_cast<int>(*w->as_uint()), static_cast<int>(*h->as_uint())));
    ok(call);
  } else if (name == "host.window.getBounds") {
    const CefRect b = window->GetBounds();
    const auto i = [](int n) { return static_cast<std::uint64_t>(n < 0 ? 0 : n); };
    link().reply(call, ipc::Writer()
                           .map(4)
                           .text("x").uint(i(b.x))
                           .text("y").uint(i(b.y))
                           .text("width").uint(i(b.width))
                           .text("height").uint(i(b.height))
                           .take());
  }
}

// ------------------------------------------------------------------------------------ dialogs

class DialogDone : public CefRunFileDialogCallback {
 public:
  DialogDone(ServiceCall call, bool many) : call_(call), many_(many) {}

  void OnFileDialogDismissed(const std::vector<CefString>& paths) override {
    ipc::Writer w;
    if (many_) {
      w.map(1).text("paths").array(paths.size());
      for (const CefString& p : paths) {
        w.text(p.ToString());
      }
    } else if (paths.empty()) {
      w.map(0);  // cancelled
    } else {
      w.map(1).text("path").text(paths.front().ToString());
    }
    link().reply(call_, w.take());
  }

 private:
  ServiceCall call_;
  bool many_;
  IMPLEMENT_REFCOUNTING(DialogDone);
};

void dialog_service(const ServiceCall& call, const std::string& name, const ipc::Value& params) {
  CefRefPtr<CefBrowser> browser = browser_state().browser;
  if (!browser) {
    link().refuse(call, "UNAVAILABLE", "there's no window");
    return;
  }
  std::vector<CefString> filters;
  if (const ipc::Value* f = params.get("filters"); f && f->type == ipc::Value::Type::array) {
    for (const ipc::Value& item : f->items) {
      if (item.as_text()) {
        filters.emplace_back(*item.as_text());
      }
    }
  }
  const ipc::Value* multiple = params.get("multiple");
  const bool many = name == "host.dialog.openFile";  // its answer is always `paths`
  cef_file_dialog_mode_t mode = FILE_DIALOG_OPEN;
  if (name == "host.dialog.openFile" && multiple && multiple->as_bool() == true) {
    mode = FILE_DIALOG_OPEN_MULTIPLE;
  } else if (name == "host.dialog.saveFile") {
    mode = FILE_DIALOG_SAVE;
  } else if (name == "host.dialog.openFolder") {
    mode = FILE_DIALOG_OPEN_FOLDER;
  }
  browser->GetHost()->RunFileDialog(mode, text_or(params, "title"), text_or(params, "defaultPath"), filters,
                                    new DialogDone(call, many));
}

// ------------------------------------------------------------------------------ notifications

void notify(const ServiceCall& call, const ipc::Value& params) {
  const std::string title = text_or(params, "title");
  if (title.empty()) {
    link().refuse(call, "INVALID_PAYLOAD", "$.title: expected text");
    return;
  }
#if defined(_WIN32)
  link().refuse(call, "UNAVAILABLE", "desktop notifications aren't implemented on Windows yet");
  return;
#else
  // The desktop's notification service through libnotify's notify-send (D-Bus directly: later).
  std::string app = "--app-name=" + browser_state().options.policy.app_id;
  std::string body = text_or(params, "body");
  std::vector<char*> argv{const_cast<char*>("notify-send"), app.data(), const_cast<char*>(title.c_str())};
  if (!body.empty()) {
    argv.push_back(body.data());
  }
  argv.push_back(nullptr);
  pid_t pid = -1;
  if (posix_spawnp(&pid, "notify-send", nullptr, nullptr, argv.data(), environ) != 0) {
    link().refuse(call, "UNAVAILABLE", "no desktop notifications here (notify-send isn't installed)");
    return;
  }
  std::thread([pid] {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
  }).detach();
  ok(call);
#endif
}

// ---------------------------------------------------------------------------------- clipboard

/// Runs `f` on the UI thread (the clipboard answers on its own).
class OnUi : public CefTask {
 public:
  explicit OnUi(std::function<void()> f) : f_(std::move(f)) {}
  void Execute() override { f_(); }

 private:
  std::function<void()> f_;
  IMPLEMENT_REFCOUNTING(OnUi);
};

void answer(const ServiceCall& call, clipboard::Result result, bool with_text) {
  CefPostTask(TID_UI, new OnUi([call, result = std::move(result), with_text] {
    if (!result.code.empty()) {
      link().refuse(call, result.code.c_str(), result.message);
    } else if (!with_text) {
      link().reply(call, std::nullopt);
    } else if (result.text) {
      link().reply(call, ipc::Writer().map(1).text("text").text(*result.text).take());
    } else {
      link().reply(call, ipc::Writer().map(0).take());  // no text on the clipboard
    }
  }));
}

void clipboard_service(const ServiceCall& call, const std::string& name, const ipc::Value& params) {
  if (name == "host.clipboard.writeText") {
    const ipc::Value* text = params.get("text");
    if (!text || !text->as_text()) {
      link().refuse(call, "INVALID_PAYLOAD", "$.text: expected text");
      return;
    }
    clipboard::write_text(*text->as_text(), [call](clipboard::Result r) { answer(call, std::move(r), false); });
  } else {
    clipboard::read_text([call](clipboard::Result r) { answer(call, std::move(r), true); });
  }
}

}  // namespace

void run_service(const ServiceCall& call, const std::string& name, const ipc::Value& params) {
  if (name.starts_with("host.window.")) {
    window_service(call, name, params);
  } else if (name.starts_with("host.dialog.")) {
    dialog_service(call, name, params);
  } else if (name.starts_with("host.clipboard.")) {
    clipboard_service(call, name, params);
  } else if (name == "host.notification.show") {
    notify(call, params);
  } else if (name == "host.app.quit") {
    ok(call);
    close_app_window();
  }
}

}  // namespace pico_desktop::host::cef
