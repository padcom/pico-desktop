// Unit tests for the host's router (docs/protocol.md §5–§7): ID renumbering per session, window
// tagging, late answers, session ends, backend restarts, subscriptions and host.echo.
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "pico_desktop/host/restart.hpp"
#include "pico_desktop/host/router.hpp"
#include "pico_desktop/ipc/cbor.hpp"

using namespace pico_desktop::host;
namespace ipc = pico_desktop::ipc;
using ipc::Kind;

static int failures = 0;

#define CHECK(expr)                                                         \
  do {                                                                      \
    if (!(expr)) {                                                          \
      std::fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #expr); \
      ++failures;                                                           \
    }                                                                       \
  } while (0)

namespace {

struct Fake : RouterOutput {
  std::vector<Bytes> backend;
  std::vector<std::pair<std::uint64_t, Bytes>> sessions;
  std::vector<std::string> lines;
  void to_backend(Bytes message) override { backend.push_back(std::move(message)); }
  void to_session(std::uint64_t session, Bytes message) override { sessions.emplace_back(session, std::move(message)); }
  void log(const std::string& line) override { lines.push_back(line); }
  void clear() {
    backend.clear();
    sessions.clear();
    lines.clear();
  }
};

ipc::EnvelopeHeader header(const Bytes& message) {
  const auto read = ipc::read_envelope(message);
  CHECK(read);
  return read ? *read.header : ipc::EnvelopeHeader{};
}

bool has_code(const Bytes& message, const char* code) {
  const auto err = ipc::raw_field(message, 5);
  if (!err) {
    return false;
  }
  const auto prefix = ipc::Writer().map(2).uint(0).text(code).take();
  return err->size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), err->begin());
}

Bytes lifecycle(const char* name) {
  return ipc::Writer().map(3).uint(0).uint(1).uint(1).uint(7).uint(3).text(name).take();
}

Bytes call(Kind kind, std::uint64_t id, const char* name, std::optional<std::uint64_t> window = std::nullopt) {
  ipc::Writer w;
  w.map(window ? 6 : 5).uint(0).uint(1).uint(1).uint(static_cast<std::uint64_t>(kind)).uint(2).uint(id);
  w.uint(3).text(name).uint(4).map(1).text("x").uint(7);
  if (window) {
    w.uint(6).uint(*window);
  }
  return w.take();
}

Bytes answer(Kind kind, std::uint64_t id, std::optional<std::uint64_t> window = std::nullopt,
             std::optional<std::uint64_t> s = std::nullopt) {
  ipc::Writer w;
  w.map(4 + (window ? 1 : 0) + (s ? 1 : 0)).uint(0).uint(1).uint(1).uint(static_cast<std::uint64_t>(kind));
  w.uint(2).uint(id);
  if (kind == Kind::error) {
    w.uint(5).map(2).uint(0).text("HANDLER_ERROR").uint(1).text("no");
  } else {
    w.uint(4).text("ok");
  }
  if (window) {
    w.uint(6).uint(*window);
  }
  if (s) {
    w.uint(8).uint(*s);
  }
  return w.take();
}

Bytes event(const char* name, std::optional<std::uint64_t> window, std::optional<std::uint64_t> s) {
  ipc::Writer w;
  w.map(3 + (window ? 1 : 0) + (s ? 1 : 0)).uint(0).uint(1).uint(1).uint(3).uint(3).text(name);
  if (window) {
    w.uint(6).uint(*window);
  }
  if (s) {
    w.uint(8).uint(*s);
  }
  return w.take();
}

Bytes control(Kind kind, std::optional<std::uint64_t> id, std::optional<std::uint64_t> s) {
  ipc::Writer w;
  w.map(2 + (id ? 1 : 0) + (s ? 1 : 0)).uint(0).uint(1).uint(1).uint(static_cast<std::uint64_t>(kind));
  if (id) {
    w.uint(2).uint(*id);
  }
  if (s) {
    w.uint(8).uint(*s);
  }
  return w.take();
}

// Two pages (sessions 1 and 2 in windows 10 and 20) and a ready backend.
void connect(Router& router, Fake& out) {
  router.from_session(1, 10, lifecycle("hello"));
  router.from_session(2, 20, lifecycle("hello"));
  router.backend_started();
  router.from_backend(lifecycle("hello"));
  out.clear();
}

void readiness() {
  Fake out;
  Router router(out);
  router.from_session(1, 10, lifecycle("hello"));
  router.from_session(1, 10, call(Kind::request, 1, "greeting.hello"));
  CHECK(out.backend.empty());
  CHECK(out.sessions.size() == 1 && has_code(out.sessions[0].second, "DISCONNECTED"));
  out.clear();

  router.backend_started();
  CHECK(!router.backend_ready());
  CHECK(out.backend.size() == 1);
  const auto hello = header(out.backend[0]);
  CHECK(hello.kind == Kind::lifecycle && hello.name == "hello");
  router.from_backend(lifecycle("hello"));
  CHECK(router.backend_ready());
  CHECK(out.sessions.size() == 1);
  const auto ready = header(out.sessions[0].second);
  CHECK(ready.kind == Kind::lifecycle && ready.name == "ready" && ready.epoch == 1u);

  // A page that says hello later hears it at once.
  out.clear();
  router.from_session(2, 20, lifecycle("hello"));
  CHECK(out.sessions.size() == 1 && out.sessions[0].first == 2);
}

void renumbering() {
  Fake out;
  Router router(out);
  connect(router, out);
  router.from_session(1, 10, call(Kind::request, 1, "greeting.hello"));
  router.from_session(2, 20, call(Kind::request, 1, "greeting.hello"));
  CHECK(out.backend.size() == 2);
  const auto a = header(out.backend[0]);
  const auto b = header(out.backend[1]);
  CHECK(a.id != b.id);
  CHECK(a.window == 10u && b.window == 20u);
  CHECK(ipc::raw_field(out.backend[0], 4) == ipc::raw_field(call(Kind::request, 1, "x"), 4));
  CHECK(router.pending_requests() == 2);

  router.from_backend(answer(Kind::response, *b.id, 20));
  CHECK(out.sessions.size() == 1 && out.sessions[0].first == 2);
  const auto reply = header(out.sessions[0].second);
  CHECK(reply.kind == Kind::response && reply.id == 1u && !reply.window);

  router.from_backend(answer(Kind::error, *a.id, 10));
  CHECK(out.sessions.size() == 2 && out.sessions[1].first == 1);
  CHECK(header(out.sessions[1].second).id == 1u && has_code(out.sessions[1].second, "HANDLER_ERROR"));
  CHECK(router.pending_requests() == 0);

  // Answered once: a repeat is dropped.
  router.from_backend(answer(Kind::response, *a.id, 10));
  CHECK(out.sessions.size() == 2);
}

void cancelling() {
  Fake out;
  Router router(out);
  connect(router, out);
  router.from_session(1, 10, call(Kind::request, 4, "slow"));
  const auto host_id = *header(out.backend[0]).id;
  router.from_session(1, 10, control(Kind::cancel, 4, std::nullopt));
  CHECK(out.backend.size() == 2);
  const auto c = header(out.backend[1]);
  CHECK(c.kind == Kind::cancel && c.id == host_id);
  // Another page can't cancel it.
  router.from_session(2, 20, control(Kind::cancel, 4, std::nullopt));
  CHECK(out.backend.size() == 2);
}

void session_end() {
  Fake out;
  Router router(out);
  connect(router, out);
  router.from_session(1, 10, call(Kind::request, 1, "slow"));
  const auto host_id = *header(out.backend[0]).id;
  router.from_backend(call(Kind::request, 7, "ui.confirm", 10));  // the backend asks page 1
  CHECK(out.sessions.size() == 1 && out.sessions[0].first == 1 && !header(out.sessions[0].second).window);
  out.clear();

  router.end_session(1);
  CHECK(out.backend.size() == 2);
  const auto c = header(out.backend[0]);
  CHECK(c.kind == Kind::cancel && c.id == host_id);
  const auto e = header(out.backend[1]);
  CHECK(e.kind == Kind::error && e.id == 7u && e.window == 10u && has_code(out.backend[1], "DISCONNECTED"));

  // The late answer has nobody to go to; the gone page's late answer isn't forwarded.
  router.from_backend(answer(Kind::response, host_id, 10));
  router.from_session(1, 10, answer(Kind::response, 7));
  CHECK(out.sessions.empty() && out.backend.size() == 2);

  // A request to the window that has no page now.
  router.from_backend(call(Kind::request, 8, "ui.confirm", 10));
  CHECK(out.backend.size() == 3 && has_code(out.backend[2], "DISCONNECTED") && header(out.backend[2]).id == 8u);
}

void reload() {
  Fake out;
  Router router(out);
  connect(router, out);
  router.from_session(1, 10, call(Kind::request, 1, "slow"));
  out.clear();
  router.from_session(3, 10, lifecycle("hello"));  // page 1 reloaded as session 3
  CHECK(out.backend.size() == 1 && header(out.backend[0]).kind == Kind::cancel);
  CHECK(out.sessions.size() == 1 && out.sessions[0].first == 3);
  out.clear();
  router.from_session(1, 10, call(Kind::request, 2, "late"));  // the old page is ignored
  CHECK(out.backend.empty() && out.sessions.empty());
}

void backend_answers() {
  Fake out;
  Router router(out);
  connect(router, out);
  router.from_backend(call(Kind::request, 5, "ui.confirm"));  // no window: the first page
  CHECK(out.sessions.size() == 1);
  const auto asked = out.sessions[0].first;
  const auto other = asked == 1 ? 2u : 1u;
  router.from_session(other, other * 10, answer(Kind::response, 5));  // not asked
  CHECK(out.backend.empty());
  router.from_session(asked, asked * 10, answer(Kind::response, 5));
  CHECK(out.backend.size() == 1);
  const auto r = header(out.backend[0]);
  CHECK(r.kind == Kind::response && r.id == 5u && r.window == asked * 10);
  router.from_session(asked, asked * 10, answer(Kind::response, 5));  // only once
  CHECK(out.backend.size() == 1);

  // A cancel from the backend reaches the page it asked.
  router.from_backend(call(Kind::request, 6, "ui.confirm", 20));
  out.clear();
  router.from_backend(control(Kind::cancel, 6, std::nullopt));
  CHECK(out.sessions.size() == 1 && out.sessions[0].first == 2 && header(out.sessions[0].second).kind == Kind::cancel);
}

void events() {
  Fake out;
  Router router(out);
  connect(router, out);
  router.from_backend(event("tick", std::nullopt, std::nullopt));
  CHECK(out.sessions.size() == 2);
  router.from_backend(event("tick", 20, std::nullopt));
  CHECK(out.sessions.size() == 3 && out.sessions[2].first == 2 && !header(out.sessions[2].second).window);
  router.from_session(1, 10, event("clicked", std::nullopt, std::nullopt));
  CHECK(out.backend.size() == 1 && header(out.backend[0]).window == 10u);
}

void subscriptions() {
  Fake out;
  Router router(out);
  connect(router, out);
  router.from_session(1, 10, call(Kind::subscribe, 5, "clock"));
  const auto sub = header(out.backend[0]);
  CHECK(sub.kind == Kind::subscribe && sub.window == 10u);
  router.from_backend(answer(Kind::response, *sub.id, 10, 3));
  const auto accepted = header(out.sessions[0].second);
  CHECK(accepted.id == 5u && accepted.subscription == 3u);

  router.from_backend(event("clock", 10, 3));
  CHECK(out.sessions.size() == 2 && out.sessions[1].first == 1);
  router.from_session(2, 20, control(Kind::unsubscribe, std::nullopt, 3));  // not page 2's
  CHECK(out.backend.size() == 1);
  router.from_session(1, 10, control(Kind::unsubscribe, std::nullopt, 3));
  CHECK(out.backend.size() == 2 && header(out.backend[1]).subscription == 3u && header(out.backend[1]).window == 10u);
  router.from_backend(event("clock", 10, 3));  // in flight when it ended: dropped
  CHECK(out.sessions.size() == 2);

  // Ended by the publisher; and a page's end unsubscribes what it had.
  router.from_session(1, 10, call(Kind::subscribe, 6, "clock"));
  router.from_backend(answer(Kind::response, *header(out.backend[2]).id, 10, 4));
  router.from_backend(control(Kind::unsubscribe, std::nullopt, 4));
  CHECK(header(out.sessions.back().second).kind == Kind::unsubscribe);
  router.from_session(1, 10, call(Kind::subscribe, 7, "clock"));
  router.from_backend(answer(Kind::response, *header(out.backend[3]).id, 10, 5));
  out.clear();
  router.end_session(1);
  CHECK(out.backend.size() == 1 && header(out.backend[0]).kind == Kind::unsubscribe &&
        header(out.backend[0]).subscription == 5u);

  // A refused subscribe opens nothing.
  router.from_session(2, 20, call(Kind::subscribe, 1, "nope"));
  router.from_backend(answer(Kind::error, *header(out.backend[1]).id, 20, 9));
  out.clear();
  router.from_backend(event("nope", 20, 9));
  CHECK(out.sessions.empty());

  // Pages don't publish.
  router.from_backend(call(Kind::subscribe, 9, "page.topic"));
  CHECK(out.backend.size() == 1 && has_code(out.backend[0], "UNKNOWN_METHOD"));
}

void restart() {
  Fake out;
  Router router(out);
  connect(router, out);
  router.from_session(1, 10, call(Kind::request, 3, "slow"));
  out.clear();
  router.backend_stopped();
  CHECK(!router.backend_ready() && router.pending_requests() == 0);
  CHECK(out.sessions.size() == 3);
  CHECK(out.sessions[0].first == 1 && header(out.sessions[0].second).id == 3u &&
        has_code(out.sessions[0].second, "DISCONNECTED"));
  CHECK(header(out.sessions[1].second).name == "disconnected" && header(out.sessions[2].second).name == "disconnected");

  out.clear();
  router.backend_started();
  router.from_backend(lifecycle("hello"));
  CHECK(router.epoch() == 2 && out.sessions.size() == 2 && header(out.sessions[0].second).epoch == 2u);

  out.clear();
  router.announce("restarting");
  CHECK(out.sessions.size() == 2 && header(out.sessions[1].second).name == "restarting");

  out.clear();
  router.closing();
  CHECK(out.backend.size() == 1 && header(out.backend[0]).name == "closing");
  CHECK(out.sessions.size() == 2 && header(out.sessions[0].second).name == "closing");
}

void refusals() {
  Fake out;
  Router router(out);
  connect(router, out);
  router.from_session(1, 10, call(Kind::request, 1, "greeting.hello", 20));  // a page naming a window
  CHECK(out.backend.empty() && out.sessions.size() == 1 && has_code(out.sessions[0].second, "INVALID_MESSAGE"));
  router.from_session(1, 10, ipc::from_hex("ff"));
  CHECK(out.backend.empty() && out.lines.size() == 2);
  router.from_session(9, 90, call(Kind::request, 1, "greeting.hello"));  // never said hello
  CHECK(out.backend.empty() && out.sessions.size() == 1);
}

void echo() {
  Fake out;
  Router router(out, true);
  router.from_session(1, 10, lifecycle("hello"));
  router.from_session(1, 10, call(Kind::request, 4, "host.echo"));  // works without a backend
  CHECK(out.backend.empty() && out.sessions.size() == 1);
  const auto& reply = out.sessions[0].second;
  CHECK(header(reply).kind == Kind::response && header(reply).id == 4u);
  CHECK(ipc::raw_field(reply, 4) == ipc::raw_field(call(Kind::request, 4, "host.echo"), 4));

  Fake plain;
  Router off(plain);
  off.from_session(1, 10, lifecycle("hello"));
  off.from_session(1, 10, call(Kind::request, 4, "host.echo"));
  CHECK(has_code(plain.sessions[0].second, "DISCONNECTED"));
}

void restart_budget() {
  using namespace std::chrono_literals;
  RestartBudget budget(RestartBudget::Limits{3, 100ms, 300ms, 10s});
  auto t = RestartBudget::Clock::time_point{};
  CHECK(budget.next(t) == 100ms);
  CHECK(budget.next(t) == 200ms);
  CHECK(budget.next(t) == 300ms);  // capped
  CHECK(!budget.next(t));          // spent
  // Ready briefly: still spent. Ready long enough: a fresh budget.
  budget.ready(t);
  CHECK(!budget.next(t + 1s));
  budget.ready(t + 1s);
  CHECK(budget.next(t + 12s) == 100ms);
}

}  // namespace

int main() {
  restart_budget();
  readiness();
  renumbering();
  cancelling();
  session_end();
  reload();
  backend_answers();
  events();
  subscriptions();
  restart();
  refusals();
  echo();
  if (failures == 0) {
    std::puts("host.router: all checks passed");
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
