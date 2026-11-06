// Routing between the frontend sessions and the backend (docs/protocol.md §1, §5–§7). A session
// is one page's Communicator (one bridge context); it ends when the page goes away. Request IDs
// from sessions are only unique per session, so the router renumbers them toward the backend and
// maps answers back; answers for a gone session or epoch are dropped here, never delivered.
// No CEF types and no threads: the CEF layer feeds it and carries out what it sends.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pico_desktop::host {

using Bytes = std::vector<std::byte>;

/// Where the router's messages go.
class RouterOutput {
 public:
  virtual ~RouterOutput() = default;
  virtual void to_backend(Bytes message) = 0;
  virtual void to_session(std::uint64_t session, Bytes message) = 0;
  virtual void log(const std::string& line) = 0;
  /// Self-tests only (`host.killBackend`): end the backend abruptly, like a crash.
  virtual void kill_backend() {}
  /// The backend answered a liveness ping.
  virtual void pong() {}
  /// The backend called a host service (`host.…`); answer with Router::answer_backend/fail_backend.
  virtual void host_request(std::uint64_t /*id*/, const std::string& /*name*/, std::optional<Bytes> /*payload*/,
                            std::optional<std::uint64_t> /*window*/) {}
  /// Whether the queue to the backend is full (pages' new requests are refused with BUSY).
  virtual bool backend_busy() const { return false; }
};

/// One line describing a message for `--trace-ipc`: its envelope fields and size, never its
/// payload or error details (they may hold user data).
std::string describe_envelope(std::span<const std::byte> message);

class Router {
 public:
  /// `self_test`: the host answers `host.echo` (with the request's payload) and `host.killBackend`
  /// itself; no app can call them otherwise.
  explicit Router(RouterOutput& out, bool self_test = false) : out_(out), self_test_(self_test) {}

  /// A message from a page's Communicator, in window `window`.
  void from_session(std::uint64_t session, std::uint64_t window, std::span<const std::byte> message);
  /// The page behind `session` went away (navigation, reload, crash, window closed).
  void end_session(std::uint64_t session);

  /// A new backend process is starting: its epoch begins and the host says hello to it (it's
  /// ready after its own hello).
  void backend_started();
  void from_backend(std::span<const std::byte> message);
  /// The backend process is gone.
  void backend_stopped();
  /// The app is closing: tell the backend and every page.
  void closing();
  /// Only the backend is asked to finish (a planned restart: the pages stay).
  void backend_closing();
  /// Tells every page a lifecycle state without an epoch (`restarting`, `failed`).
  void announce(const char* name);
  /// A liveness ping to the backend (it answers `pong` with the same `seq`).
  void ping(std::uint64_t seq);
  /// Answers the backend's host-service request `id` (with `payload`, raw CBOR, or none).
  void answer_backend(std::uint64_t id, std::optional<Bytes> payload, std::optional<std::uint64_t> window);
  /// Fails the backend's host-service request `id`.
  void fail_backend(std::uint64_t id, const char* code, const std::string& message, std::optional<std::uint64_t> window);

  bool backend_ready() const { return ready_; }
  std::uint64_t epoch() const { return epoch_; }
  std::size_t pending_requests() const { return ui_requests_.size(); }

 private:
  struct UiRequest {
    std::uint64_t session;
    std::uint64_t ui_id;
    bool subscribe;
  };

  void session_hello(std::uint64_t session, std::uint64_t window);
  void session_request(std::uint64_t session, std::uint64_t window, std::span<const std::byte> message,
                       std::uint64_t ui_id, bool subscribe, const std::string& name);
  void session_answer(std::uint64_t session, std::uint64_t window, std::span<const std::byte> message, std::uint64_t id);
  void session_cancel(std::uint64_t session, std::uint64_t ui_id);
  void session_unsubscribe(std::uint64_t session, std::uint64_t window, std::span<const std::byte> message,
                           std::uint64_t s);
  /// Answers request `ui_id` of `session` itself, with `payload` (raw CBOR) or none.
  void answer_here(std::uint64_t session, std::uint64_t ui_id, std::optional<std::vector<std::byte>> payload);

  void backend_answer(std::span<const std::byte> message, std::uint64_t id, std::optional<std::uint64_t> s);
  void backend_request(std::span<const std::byte> message, std::uint64_t id, std::optional<std::uint64_t> window);
  void backend_event(std::span<const std::byte> message, std::optional<std::uint64_t> s,
                     std::optional<std::uint64_t> window);
  void backend_ended_subscription(std::span<const std::byte> message, std::uint64_t s);
  void backend_cancel(std::span<const std::byte> message, std::uint64_t id);

  /// The sessions a backend message without a known target goes to (one, or every current one).
  std::vector<std::uint64_t> targets(std::optional<std::uint64_t> window) const;
  void deliver(std::uint64_t session, std::span<const std::byte> message);
  void refuse_session(std::uint64_t session, std::uint64_t id, const char* code, const std::string& message);
  bool is_current(std::uint64_t session) const;

  RouterOutput& out_;
  bool self_test_;
  bool ready_ = false;
  std::uint64_t epoch_ = 0;
  std::uint64_t next_id_ = 1;
  std::map<std::uint64_t, std::uint64_t> session_window_;  // session → window
  std::map<std::uint64_t, std::uint64_t> current_;         // window → its current session
  std::map<std::uint64_t, UiRequest> ui_requests_;         // host ID → a session's request
  std::map<std::uint64_t, std::uint64_t> backend_requests_;  // backend ID → the session asked
  std::map<std::uint64_t, std::uint64_t> subscriptions_;     // publisher's s → subscriber session
};

}  // namespace pico_desktop::host
