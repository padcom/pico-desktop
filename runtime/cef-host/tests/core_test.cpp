// Unit tests for the host's CEF-free core: asset resolution (traversal), MIME types, the
// navigation/bridge policy and option parsing.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#include "pico_desktop/host/assets.hpp"
#include "pico_desktop/host/console_event.hpp"
#include "pico_desktop/host/options.hpp"
#include "pico_desktop/host/policy.hpp"
#include "pico_desktop/host/sandbox.hpp"
#include "pico_desktop/host/services.hpp"

#include <set>

namespace fs = std::filesystem;
using namespace pico_desktop::host;
using namespace std::string_literals;

static int failures = 0;

#define CHECK(expr)                                                  \
  do {                                                               \
    if (!(expr)) {                                                   \
      std::fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #expr); \
      ++failures;                                                    \
    }                                                                \
  } while (0)

static void write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << text;
}

static void assets(const fs::path& tmp) {
  const fs::path root = tmp / "app";
  write(root / "index.html", "<!doctype html>");
  write(root / "assets" / "main.js", "1");
  write(root / "sub" / "index.html", "sub");
  write(root / "a b.txt", "space");
  write(tmp / "secret.txt", "outside");
  fs::create_directories(root / "empty");
  std::error_code ec;
  fs::create_symlink(tmp / "secret.txt", root / "link-out.txt", ec);
  fs::create_symlink(root / "assets" / "main.js", root / "link-in.js", ec);
  const fs::path canon = fs::canonical(root);

  CHECK(resolve_asset(root, "/") == canon / "index.html");
  CHECK(resolve_asset(root, "/index.html") == canon / "index.html");
  CHECK(resolve_asset(root, "/assets/main.js") == canon / "assets" / "main.js");
  CHECK(resolve_asset(root, "/assets/main.js?v=1#x") == canon / "assets" / "main.js");
  CHECK(resolve_asset(root, "/sub/") == canon / "sub" / "index.html");
  CHECK(resolve_asset(root, "/sub") == canon / "sub" / "index.html");
  CHECK(resolve_asset(root, "/a%20b.txt") == canon / "a b.txt");
  CHECK(resolve_asset(root, "/./assets//main.js") == canon / "assets" / "main.js");
  CHECK(resolve_asset(root, "/link-in.js") == canon / "assets" / "main.js");

  // Traversal and other escapes.
  CHECK(!resolve_asset(root, "/../secret.txt"));
  CHECK(!resolve_asset(root, "/assets/../../secret.txt"));
  CHECK(!resolve_asset(root, "/%2e%2e/secret.txt"));
  CHECK(!resolve_asset(root, "/%2E%2E/secret.txt"));
  CHECK(!resolve_asset(root, "/assets%2f..%2f..%2fsecret.txt"));
  CHECK(!resolve_asset(root, "/..%5csecret.txt"));
  CHECK(!resolve_asset(root, "/index.html%00.js"));
  CHECK(!resolve_asset(root, "/bad%zz"));
  CHECK(!resolve_asset(root, "/trailing%2"));
  CHECK(!resolve_asset(root, "/link-out.txt"));
  CHECK(!resolve_asset(root, "relative.html"));
  CHECK(!resolve_asset(root, ""));
  // Missing files and directories without an index.
  CHECK(!resolve_asset(root, "/missing.js"));
  CHECK(!resolve_asset(root, "/empty/"));
  CHECK(!resolve_asset(tmp / "no-such-root", "/index.html"));

  CHECK(percent_decode("a%2Fb") == std::string("a/b"));
  CHECK(!percent_decode("%"));
  CHECK(!percent_decode("%4"));
  CHECK(!percent_decode("%00"));

  CHECK(mime_type_for("x/index.HTML") == "text/html");
  CHECK(mime_type_for("main.js") == "text/javascript");
  CHECK(mime_type_for("style.css") == "text/css");
  CHECK(mime_type_for("logo.svg") == "image/svg+xml");
  CHECK(mime_type_for("blob.bin") == "application/octet-stream");
}

static void policy() {
  Policy defaults;
  CHECK(defaults.scheme == OriginScheme::https);
  Policy p;
  p.app_id = "hello";
  p.scheme = OriginScheme::app;
  CHECK(trusted_origin(p) == "app://hello");
  CHECK(start_url(p) == "app://hello/index.html");
  CHECK(is_trusted_url(p, "app://hello/"));
  CHECK(is_trusted_url(p, "app://hello/assets/x.js?q"));
  CHECK(!is_trusted_url(p, "app://hello.evil/"));
  CHECK(!is_trusted_url(p, "app://hellothere/"));
  CHECK(!is_trusted_url(p, "app://other/"));
  CHECK(!is_trusted_url(p, "https://hello.localhost/"));

  CHECK(decide_navigation(p, "app://hello/page.html") == Navigation::allow);
  CHECK(decide_navigation(p, "https://example.com/") == Navigation::open_external);
  CHECK(decide_navigation(p, "http://example.com/") == Navigation::open_external);
  CHECK(decide_navigation(p, "mailto:a@b.c") == Navigation::open_external);
  CHECK(decide_navigation(p, "file:///etc/passwd") == Navigation::block);
  CHECK(decide_navigation(p, "data:text/html,hi") == Navigation::block);
  CHECK(decide_navigation(p, "javascript:alert(1)") == Navigation::block);
  CHECK(decide_navigation(p, "chrome://settings") == Navigation::block);

  CHECK(bridge_allowed(p, "app://hello/index.html", true));
  CHECK(!bridge_allowed(p, "app://hello/frame.html", false));
  CHECK(!bridge_allowed(p, "about:srcdoc", false));
  CHECK(!bridge_allowed(p, "https://example.com/", true));
  Policy sub = p;
  sub.bridge_in_subframes = true;
  CHECK(bridge_allowed(sub, "app://hello/frame.html", false));
  CHECK(!bridge_allowed(sub, "data:text/html,x", false));

  // Permission prompts: only reading the clipboard, only for the app, only when declared.
  CHECK(permission_granted(p, "app://hello", true, true));
  CHECK(!permission_granted(p, "app://hello", true, false));
  CHECK(!permission_granted(p, "app://hello", false, true));
  CHECK(!permission_granted(p, "https://example.com", true, true));
  CHECK(!permission_granted(p, "app://hello.evil", true, true));

  Policy https = p;
  https.scheme = OriginScheme::https;
  CHECK(trusted_origin(https) == "https://hello.localhost");
  CHECK(is_trusted_url(https, "https://hello.localhost/index.html"));
  CHECK(!is_trusted_url(https, "https://hello.localhost.evil.com/"));
  CHECK(decide_navigation(https, "https://example.com/") == Navigation::open_external);

  Policy dev = p;
  dev.dev_origin = "http://localhost:5173";
  CHECK(start_url(dev) == "http://localhost:5173/");
  CHECK(decide_navigation(dev, "http://localhost:5173/src/main.ts") == Navigation::allow);
  CHECK(decide_navigation(dev, "http://localhost:5174/") == Navigation::open_external);

  CHECK(is_valid_app_id("hello"));
  CHECK(is_valid_app_id("com.example.hello-world"));
  CHECK(!is_valid_app_id(""));
  CHECK(!is_valid_app_id("Hello"));
  CHECK(!is_valid_app_id("a..b"));
  CHECK(!is_valid_app_id("-a"));
  CHECK(!is_valid_app_id("a-"));
  CHECK(!is_valid_app_id("a/b"));
  CHECK(!is_valid_app_id("a:1"));
}

static ParseResult parse(std::vector<std::string> args) {
  return parse_options(args);
}

static void options() {
  {
    const auto r = parse({"--app-dir=/x", "--app-id=com.example.app", "--origin=https"});
    CHECK(std::holds_alternative<Options>(r));
    if (const auto* o = std::get_if<Options>(&r)) {
      CHECK(o->app_dir == "/x");
      CHECK(o->policy.app_id == "com.example.app");
      CHECK(o->policy.scheme == OriginScheme::https);
      CHECK(o->mode == Mode::run);
      CHECK(!o->dev);
      CHECK(o->open_external);
      CHECK(o->backend.empty());
    }
  }
  {
    const auto r = parse({"--app-dir=/x", "--dev", "--control=stdin"});
    const auto* o = std::get_if<Options>(&r);
    CHECK(o && o->control_stdin);
  }
  CHECK(std::holds_alternative<std::string>(parse({"--app-dir=/x", "--control=stdin"})));  // needs --dev
  CHECK(std::holds_alternative<std::string>(parse({"--app-dir=/x", "--dev", "--control=tcp"})));
  {
    const auto r = parse({"--app-dir=/x", "--multi-instance"});
    const auto* o = std::get_if<Options>(&r);
    CHECK(o && o->multi_instance);
  }
  {
    const auto r = parse({"--app-dir=/x", "--liveness=500/1500"});
    const auto* o = std::get_if<Options>(&r);
    CHECK(o && o->liveness_interval_ms == 500 && o->liveness_timeout_ms == 1500);
    const auto d = parse({"--app-dir=/x"});
    CHECK(std::get<Options>(d).liveness_interval_ms == 5000 && std::get<Options>(d).liveness_timeout_ms == 15000);
  }
  {
    const auto r = parse({"--app-dir=/x", "--liveness=off"});
    const auto* o = std::get_if<Options>(&r);
    CHECK(o && o->liveness_interval_ms == 0);
  }
  for (const char* bad : {"--liveness=500", "--liveness=500/400", "--liveness=50/1000", "--liveness=a/b", "--liveness=-1/5"}) {
    CHECK(std::holds_alternative<std::string>(parse({"--app-dir=/x", bad})));
  }
  {
    const auto r = parse({"--app-dir=/x", "--capabilities=window,notifications"});
    const auto* o = std::get_if<Options>(&r);
    const std::set<Capability> expected{Capability::window, Capability::notifications};
    CHECK(o && o->capabilities == expected);
    CHECK(std::get<Options>(parse({"--app-dir=/x"})).capabilities.empty());
    CHECK(std::holds_alternative<std::string>(parse({"--app-dir=/x", "--capabilities=window,camera"})));
  }
  CHECK(service_info("host.window.setTitle").known && service_info("host.window.setTitle").needs == Capability::window);
  CHECK(service_info("host.dialog.openFile").needs == Capability::dialogs);
  CHECK(service_info("host.app.quit").known && !service_info("host.app.quit").needs);
  // The clipboard, like the page's: writing needs nothing, reading the `clipboard` capability.
  CHECK(service_info("host.clipboard.writeText").known && !service_info("host.clipboard.writeText").needs);
  CHECK(service_info("host.clipboard.readText").needs == Capability::clipboard);
  CHECK(!service_info("host.fs.delete").known);
  {
    const auto r = parse({"--app-dir=/x", "--window-size=800x600", "--window-title=My App", "--close-grace=500"});
    const auto* o = std::get_if<Options>(&r);
    CHECK(o && o->window_width == 800 && o->window_height == 600 && o->window_title == "My App" && o->close_grace_ms == 500);
    const auto d = std::get<Options>(parse({"--app-dir=/x"}));
    CHECK(d.window_width == 1024 && d.window_height == 768 && d.window_title.empty() && d.close_grace_ms == 3000);
  }
  for (const char* bad : {"--window-size=800", "--window-size=10x10", "--window-size=axb", "--close-grace=-1",
                          "--close-grace=99999999"}) {
    CHECK(std::holds_alternative<std::string>(parse({"--app-dir=/x", bad})));
  }
  CHECK(parse_control("restart-backend") == ControlCommand::restart_backend);
  CHECK(parse_control("  reload\r\n") == ControlCommand::reload);
  CHECK(parse_control("quit") == ControlCommand::quit);
  CHECK(!parse_control(""));
  CHECK(!parse_control("   "));
  CHECK(!parse_control("restart"));
  {
    const auto r = parse({"--app-dir=/x", "--backend=/opt/app/backend"});
    const auto* o = std::get_if<Options>(&r);
    CHECK(o && o->backend == "/opt/app/backend");
  }
  {
    const auto r = parse({"--app-dir=/x", "--self-test", "--no-external-open", "--enable-logging"});
    const auto* o = std::get_if<Options>(&r);
    CHECK(o && o->mode == Mode::self_test && !o->open_external);
  }
  CHECK(std::holds_alternative<std::string>(parse({})));
  CHECK(std::holds_alternative<std::string>(parse({"--app-dir=/x", "--app-id=Bad"})));
  CHECK(std::holds_alternative<std::string>(parse({"--app-dir=/x", "--origin=file"})));
  CHECK(std::holds_alternative<std::string>(parse({"--app-dir=/x", "stray"})));
  // Production never opens a debugging port; the dev server needs the dev policy.
  CHECK(std::holds_alternative<std::string>(parse({"--app-dir=/x", "--remote-debugging-port=9222"})));
  CHECK(std::holds_alternative<Options>(parse({"--app-dir=/x", "--dev", "--remote-debugging-port=9222"})));
  CHECK(std::holds_alternative<std::string>(parse({"--dev-url=http://localhost:5173"})));
  CHECK(std::holds_alternative<std::string>(parse({"--dev", "--dev-url=http://example.com:80"})));
  {
    const auto r = parse({"--dev", "--dev-url=http://localhost:5173/"});
    const auto* o = std::get_if<Options>(&r);
    CHECK(o && o->policy.dev_origin == std::string("http://localhost:5173"));
  }
  {
    const auto r = parse({"--version", "stray"});
    const auto* o = std::get_if<Options>(&r);
    CHECK(o && o->mode == Mode::version);
  }
}

static void sandbox() {
  const auto sandboxed = parse_proc_status("Name:\tchrome\nSeccomp:\t2\nSeccomp_filters:\t1\nNSpid:\t4242\t1\n");
  CHECK(sandboxed.seccomp_mode == 2);
  CHECK(sandboxed.nspid_levels == 2);
  const auto plain = parse_proc_status("Seccomp:\t0\nNSpid:\t17\n");
  CHECK(plain.seccomp_mode == 0);
  CHECK(plain.nspid_levels == 1);
  CHECK(parse_proc_status("Name: x\n").seccomp_mode == -1);

  CHECK(parse_proc_stat_ppid("123 (pico-desktop-host) S 77 123 123 0") == 77);
  CHECK(parse_proc_stat_ppid("9 (we ird) (name)) R 5 9") == 5);
  CHECK(!parse_proc_stat_ppid("garbage"));

  const std::string cmdline = "/app/host\0--type=renderer\0--lang=en\0"s;
  CHECK(parse_cmdline_type(cmdline) == "renderer");
  CHECK(parse_cmdline_type("/app/host\0--app-dir=x\0"s).empty());
  CHECK(parse_cmdline_type("/app/host --type=zygote --no-zygote-sandbox") == "zygote");

  CHECK(renderers_sandboxed({{1, "renderer", 2, true}, {2, "gpu-process", 0, false}}));
  CHECK(!renderers_sandboxed({{1, "renderer", 2, false}}));
  CHECK(!renderers_sandboxed({{1, "renderer", 0, true}}));
  CHECK(!renderers_sandboxed({{2, "gpu-process", 2, true}}));
  CHECK(!renderers_sandboxed({}));

  // Windows (#11): every renderer below Medium integrity and in a job, and at least one.
  CHECK(windows_renderers_sandboxed({{1, "renderer", 0x0000, true}, {2, "gpu-process", 0x2000, false}}));
  CHECK(windows_renderers_sandboxed({{1, "renderer", 0x1000, true}, {2, "renderer", 0x0000, true}}));
  CHECK(!windows_renderers_sandboxed({{1, "renderer", 0x2000, true}}));  // Medium: the user's own rights
  CHECK(!windows_renderers_sandboxed({{1, "renderer", 0x0000, false}}));  // not in a job
  CHECK(!windows_renderers_sandboxed({{1, "renderer", -1, true}}));  // the token couldn't be read
  CHECK(!windows_renderers_sandboxed({{1, "renderer", 0x0000, true}, {2, "renderer", 0x3000, true}}));
  CHECK(!windows_renderers_sandboxed({{2, "gpu-process", 0x0000, true}}));  // no renderer
  CHECK(!windows_renderers_sandboxed({}));
  CHECK(!suid_sandbox_usable("/nonexistent/chrome-sandbox"));
  CHECK(!suid_sandbox_usable("/bin/sh"));  // not setuid
  CHECK(sandbox_unavailable_message("/x/chrome-sandbox").find("user namespaces") != std::string::npos);
}

// Windows console events: the ones after which the process is ended have the handler wait (#18).
static void console_events() {
  CHECK(ends_process_on_return(2));  // CTRL_CLOSE_EVENT
  CHECK(ends_process_on_return(5));  // CTRL_LOGOFF_EVENT
  CHECK(ends_process_on_return(6));  // CTRL_SHUTDOWN_EVENT
  CHECK(!ends_process_on_return(0));  // CTRL_C_EVENT
  CHECK(!ends_process_on_return(1));  // CTRL_BREAK_EVENT
  CHECK(!ends_process_on_return(3));
}

int main() {
  const fs::path tmp = fs::temp_directory_path() /
                       ("pd-host-core-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  assets(tmp);
  policy();
  options();
  sandbox();
  console_events();
  fs::remove_all(tmp);
  if (failures == 0) {
    std::puts("host.core: all checks passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
