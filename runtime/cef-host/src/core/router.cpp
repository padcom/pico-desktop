#include "pico_desktop/host/router.hpp"

#include "pico_desktop/ipc/cbor.hpp"

namespace pico_desktop::host {

namespace ipc = pico_desktop::ipc;
using ipc::Kind;

namespace {

constexpr std::uint64_t kKeySubscription = 8;

Bytes lifecycle(const char* name, std::optional<std::uint64_t> epoch) {
  ipc::Writer w;
  w.map(epoch ? 4 : 3).uint(0).uint(1).uint(1).uint(static_cast<std::uint64_t>(Kind::lifecycle)).uint(3).text(name);
  if (epoch) {
    w.uint(7).uint(*epoch);
  }
  return w.take();
}

Bytes error(std::uint64_t id, const char* code, const std::string& message, std::optional<std::uint64_t> window) {
  ipc::Writer w;
  w.map(window ? 5 : 4).uint(0).uint(1).uint(1).uint(static_cast<std::uint64_t>(Kind::error)).uint(2).uint(id);
  w.uint(5).map(2).uint(0).text(code).uint(1).text(message);
  if (window) {
    w.uint(ipc::kKeyWindow).uint(*window);
  }
  return w.take();
}

Bytes cancel(std::uint64_t id) {
  return ipc::Writer().map(3).uint(0).uint(1).uint(1).uint(static_cast<std::uint64_t>(Kind::cancel)).uint(2).uint(id).take();
}

Bytes unsubscribe(std::uint64_t s, std::uint64_t window) {
  return ipc::Writer()
      .map(4)
      .uint(0).uint(1)
      .uint(1).uint(static_cast<std::uint64_t>(Kind::unsubscribe))
      .uint(kKeySubscription).uint(s)
      .uint(ipc::kKeyWindow).uint(window)
      .take();
}

}  // namespace

// ---------------------------------------------------------------------------------- sessions

void Router::from_session(std::uint64_t session, std::uint64_t window, std::span<const std::byte> message) {
  const auto read = ipc::read_envelope(message);
  if (!read) {
    out_.log("pd-host: dropped an invalid message from a page: " + read.refusal.message);
    return;
  }
  const auto& h = *read.header;
  if (h.window) {
    out_.log("pd-host: refused a page's message that names a window");
    if (h.kind == Kind::request || h.kind == Kind::subscribe) {
      refuse_session(session, *h.id, "INVALID_MESSAGE", "pages can't address windows");
    }
    return;
  }
  if (h.kind == Kind::lifecycle) {
    if (h.name == std::string("hello")) {
      session_hello(session, window);
    }
    return;
  }
  if (!is_current(session)) {
    return;  // a page that never said hello, or one that was replaced
  }
  switch (h.kind) {
    case Kind::request:
    case Kind::subscribe:
      session_request(session, window, message, *h.id, h.kind == Kind::subscribe, *h.name);
      break;
    case Kind::response:
    case Kind::error:
      session_answer(session, window, message, *h.id);
      break;
    case Kind::event:
      if (ready_) {
        out_.to_backend(ipc::with_window(message, window));
      }
      break;
    case Kind::cancel:
      session_cancel(session, *h.id);
      break;
    case Kind::unsubscribe:
      session_unsubscribe(session, window, message, *h.subscription);
      break;
    case Kind::lifecycle:
      break;
  }
}

void Router::session_hello(std::uint64_t session, std::uint64_t window) {
  if (const auto it = current_.find(window); it != current_.end() && it->second != session) {
    end_session(it->second);
  }
  current_[window] = session;
  session_window_[session] = window;
  if (ready_) {
    out_.to_session(session, lifecycle("ready", epoch_));
  }
}

void Router::session_request(std::uint64_t session, std::uint64_t window, std::span<const std::byte> message,
                             std::uint64_t ui_id, bool subscribe, const std::string& name) {
  if (host_echo_ && !subscribe && name == "host.echo") {
    echo(session, message, ui_id);
    return;
  }
  if (!ready_) {
    refuse_session(session, ui_id, "DISCONNECTED", "the backend isn't ready");
    return;
  }
  const std::uint64_t id = next_id_++;
  ui_requests_[id] = {session, ui_id, subscribe};
  out_.to_backend(ipc::with_window(ipc::with_field(message, ipc::kKeyId, id), window));
}

void Router::echo(std::uint64_t session, std::span<const std::byte> message, std::uint64_t ui_id) {
  const auto payload = ipc::raw_field(message, 4);
  ipc::Writer w;
  w.map(payload ? 4 : 3).uint(0).uint(1).uint(1).uint(static_cast<std::uint64_t>(Kind::response)).uint(2).uint(ui_id);
  if (payload) {
    w.uint(4).raw(*payload);
  }
  out_.to_session(session, w.take());
}

void Router::session_answer(std::uint64_t session, std::uint64_t window, std::span<const std::byte> message,
                            std::uint64_t id) {
  const auto it = backend_requests_.find(id);
  if (it == backend_requests_.end() || it->second != session) {
    return;  // not a question this page was asked (or already answered)
  }
  backend_requests_.erase(it);
  out_.to_backend(ipc::with_window(message, window));
}

void Router::session_cancel(std::uint64_t session, std::uint64_t ui_id) {
  for (const auto& [id, request] : ui_requests_) {
    if (request.session == session && request.ui_id == ui_id) {
      out_.to_backend(cancel(id));
      return;
    }
  }
}

void Router::session_unsubscribe(std::uint64_t session, std::uint64_t window, std::span<const std::byte> message,
                                 std::uint64_t s) {
  const auto it = subscriptions_.find(s);
  if (it != subscriptions_.end() && it->second == session) {
    subscriptions_.erase(it);
    out_.to_backend(ipc::with_window(message, window));
  }
}

void Router::end_session(std::uint64_t session) {
  for (auto it = ui_requests_.begin(); it != ui_requests_.end();) {
    if (it->second.session == session) {
      out_.to_backend(cancel(it->first));  // the answer has nobody to go to
      it = ui_requests_.erase(it);
    } else {
      ++it;
    }
  }
  const auto window = session_window_.count(session) ? session_window_[session] : 0;
  for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
    if (it->second == session) {
      out_.to_backend(unsubscribe(it->first, window));
      it = subscriptions_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = backend_requests_.begin(); it != backend_requests_.end();) {
    if (it->second == session) {
      out_.to_backend(error(it->first, "DISCONNECTED", "the page went away", window));
      it = backend_requests_.erase(it);
    } else {
      ++it;
    }
  }
  if (const auto it = current_.find(window); it != current_.end() && it->second == session) {
    current_.erase(it);
  }
  session_window_.erase(session);
}

// ----------------------------------------------------------------------------------- backend

void Router::backend_started() {
  ++epoch_;
  ready_ = false;
}

void Router::from_backend(std::span<const std::byte> message) {
  const auto read = ipc::read_envelope(message);
  if (!read) {
    out_.log("pd-host: dropped an invalid message from the backend: " + read.refusal.message);
    return;
  }
  const auto& h = *read.header;
  switch (h.kind) {
    case Kind::lifecycle:
      if (h.name == std::string("hello") && !ready_) {
        ready_ = true;
        for (const auto& [window, session] : current_) {
          out_.to_session(session, lifecycle("ready", epoch_));
        }
      }
      break;
    case Kind::response:
    case Kind::error:
      // Only an accepting response opens a subscription.
      backend_answer(message, *h.id, h.kind == Kind::response ? h.subscription : std::nullopt);
      break;
    case Kind::request:
      backend_request(message, *h.id, h.window);
      break;
    case Kind::event:
      backend_event(message, h.subscription, h.window);
      break;
    case Kind::unsubscribe:
      backend_ended_subscription(message, *h.subscription);
      break;
    case Kind::cancel:
      backend_cancel(message, *h.id);
      break;
    case Kind::subscribe:
      out_.to_backend(error(*h.id, "UNKNOWN_METHOD", "pages don't publish topics", h.window));
      break;
  }
}

void Router::backend_answer(std::span<const std::byte> message, std::uint64_t id, std::optional<std::uint64_t> s) {
  const auto it = ui_requests_.find(id);
  if (it == ui_requests_.end()) {
    return;  // a late answer (the asker went away, or it was cancelled)
  }
  const UiRequest request = it->second;
  ui_requests_.erase(it);
  if (request.subscribe && s) {
    subscriptions_[*s] = request.session;
  }
  deliver(request.session, ipc::with_field(message, ipc::kKeyId, request.ui_id));
}

void Router::backend_request(std::span<const std::byte> message, std::uint64_t id, std::optional<std::uint64_t> window) {
  const auto sessions = targets(window);
  if (sessions.empty()) {
    out_.to_backend(error(id, "DISCONNECTED", "no page is open", window));
    return;
  }
  // v1 has one window; with several, the first answers.
  backend_requests_[id] = sessions.front();
  deliver(sessions.front(), message);
}

void Router::backend_event(std::span<const std::byte> message, std::optional<std::uint64_t> s,
                           std::optional<std::uint64_t> window) {
  if (s) {
    if (const auto it = subscriptions_.find(*s); it != subscriptions_.end()) {
      deliver(it->second, message);
    }
    return;
  }
  for (const auto session : targets(window)) {
    deliver(session, message);
  }
}

void Router::backend_ended_subscription(std::span<const std::byte> message, std::uint64_t s) {
  const auto it = subscriptions_.find(s);
  if (it != subscriptions_.end()) {
    const auto session = it->second;
    subscriptions_.erase(it);
    deliver(session, message);
  }
}

void Router::backend_cancel(std::span<const std::byte> message, std::uint64_t id) {
  const auto it = backend_requests_.find(id);
  if (it != backend_requests_.end()) {
    deliver(it->second, message);
  }
}

void Router::backend_stopped() {
  ready_ = false;
  for (const auto& [id, request] : ui_requests_) {
    if (is_current(request.session)) {
      out_.to_session(request.session,
                      error(request.ui_id, "DISCONNECTED", "the backend stopped", std::nullopt));
    }
  }
  ui_requests_.clear();
  backend_requests_.clear();
  subscriptions_.clear();
  for (const auto& [window, session] : current_) {
    out_.to_session(session, lifecycle("disconnected", std::nullopt));
  }
}

void Router::closing() {
  if (ready_) {
    out_.to_backend(lifecycle("closing", std::nullopt));
  }
  for (const auto& [window, session] : current_) {
    out_.to_session(session, lifecycle("closing", std::nullopt));
  }
}

// ----------------------------------------------------------------------------------- helpers

std::vector<std::uint64_t> Router::targets(std::optional<std::uint64_t> window) const {
  std::vector<std::uint64_t> sessions;
  if (window) {
    if (const auto it = current_.find(*window); it != current_.end()) {
      sessions.push_back(it->second);
    }
    return sessions;
  }
  for (const auto& [w, session] : current_) {
    sessions.push_back(session);
  }
  return sessions;
}

void Router::deliver(std::uint64_t session, std::span<const std::byte> message) {
  if (is_current(session)) {
    out_.to_session(session, ipc::without_window(message));
  }
}

void Router::refuse_session(std::uint64_t session, std::uint64_t id, const char* code, const std::string& message) {
  out_.to_session(session, error(id, code, message, std::nullopt));
}

bool Router::is_current(std::uint64_t session) const {
  const auto it = session_window_.find(session);
  if (it == session_window_.end()) {
    return false;
  }
  const auto current = current_.find(it->second);
  return current != current_.end() && current->second == session;
}

}  // namespace pico_desktop::host
