// When to start the backend again after it exits (docs/protocol.md §7: bounded backoff, then
// `failed`). A backend that stayed ready long enough earns a fresh budget, so an app that crashes
// once a day restarts every time, and one that crashes on start gives up quickly.
#pragma once

#include <chrono>
#include <optional>

namespace pico_desktop::host {

class RestartBudget {
 public:
  using Clock = std::chrono::steady_clock;
  using Delay = std::chrono::milliseconds;

  struct Limits {
    int restarts = 5;
    Delay first{250};
    Delay longest{4000};
    Delay stable{30000};
  };

  RestartBudget() = default;
  explicit RestartBudget(Limits limits) : limits_(limits) {}

  /// A backend completed its handshake at `now`.
  void ready(Clock::time_point now) { ready_at_ = now; }

  /// The backend exited at `now`: the delay before starting it again, or nothing once spent.
  std::optional<Delay> next(Clock::time_point now) {
    if (ready_at_ && now - *ready_at_ >= limits_.stable) {
      used_ = 0;
    }
    ready_at_.reset();
    if (used_ >= limits_.restarts) {
      return std::nullopt;
    }
    Delay delay = limits_.first;
    for (int i = 0; i < used_ && delay < limits_.longest; ++i) {
      delay *= 2;
    }
    ++used_;
    return delay < limits_.longest ? delay : limits_.longest;
  }

 private:
  Limits limits_;
  int used_ = 0;
  std::optional<Clock::time_point> ready_at_;
};

}  // namespace pico_desktop::host
