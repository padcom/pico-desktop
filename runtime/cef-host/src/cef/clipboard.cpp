#include "clipboard.hpp"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <xcb/xcb.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace pico_desktop::host::cef::clipboard {

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kReadTimeout = std::chrono::seconds(3);
constexpr auto kHandOffTimeout = std::chrono::seconds(1);

template <typename T>
using Reply = std::unique_ptr<T, decltype(&std::free)>;

template <typename T>
Reply<T> reply(T* r) {
  return Reply<T>(r, &std::free);
}

Result failed(const char* code, std::string message) { return Result{std::nullopt, code, std::move(message)}; }

class X11 {
 public:
  X11() : wake_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {}
  ~X11() {
    stop();
    ::close(wake_);
  }

  void post(std::function<void()> job) {
    {
      std::lock_guard lock(mutex_);
      if (!started_) {
        started_ = true;
        thread_ = std::thread([this] { run(); });
      }
      jobs_.push_back(std::move(job));
    }
    wake();
  }

  void stop() {
    {
      std::lock_guard lock(mutex_);
      if (!started_ || stopping_) {
        return;
      }
      stopping_ = true;
    }
    wake();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  // ------------------------------------------------------------------ on the clipboard's thread

  void write(std::string text, const Done& done) {
    if (!connected(done)) {
      return;
    }
    if (text.size() > kMaxText || text.size() + 64 > max_request_bytes_) {
      done(failed("TOO_LARGE", "the text is over the clipboard's 1 MiB limit"));
      return;
    }
    // A selection is owned from a server time (ICCCM): a zero-length append to our own window's
    // property comes back as a PropertyNotify that carries it.
    pending_writes_.push_back({std::move(text), done});
    xcb_change_property(conn_, XCB_PROP_MODE_APPEND, window_, atoms_.time, XCB_ATOM_STRING, 8, 0, nullptr);
    xcb_flush(conn_);
  }

  void read(const Done& done) {
    if (!connected(done)) {
      return;
    }
    if (owned_) {
      done(Result{owned_, {}, {}});
      return;
    }
    reads_.push_back(done);
    if (reads_.size() == 1) {
      start_read();
    }
  }

 private:
  struct Atoms {
    xcb_atom_t clipboard, utf8, targets, timestamp, text, incr, property, time, manager, save_targets;
  };
  struct PendingWrite {
    std::string text;
    Done done;
  };

  void wake() {
    const std::uint64_t one = 1;
    [[maybe_unused]] auto n = ::write(wake_, &one, sizeof one);
  }

  bool connected(const Done& done) {
    if (!conn_) {
      done(failed("UNAVAILABLE", "no clipboard here: it needs an X11 display (or XWayland)"));
      return false;
    }
    return true;
  }

  void connect() {
    int screen_number = 0;
    xcb_connection_t* conn = xcb_connect(nullptr, &screen_number);
    if (xcb_connection_has_error(conn) != 0) {
      xcb_disconnect(conn);
      return;
    }
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(conn));
    for (int i = 0; i < screen_number && it.rem > 0; ++i) {
      xcb_screen_next(&it);
    }
    conn_ = conn;
    window_ = xcb_generate_id(conn_);
    const std::uint32_t events = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(conn_, XCB_COPY_FROM_PARENT, window_, it.data->root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                      it.data->root_visual, XCB_CW_EVENT_MASK, &events);
    atoms_ = Atoms{atom("CLIPBOARD"),   atom("UTF8_STRING"),   atom("TARGETS"),
                   atom("TIMESTAMP"),   atom("TEXT"),          atom("INCR"),
                   atom("PD_CLIPBOARD"), atom("PD_CLIPBOARD_TIME"), atom("CLIPBOARD_MANAGER"),
                   atom("SAVE_TARGETS")};
    // In bytes; with BIG-REQUESTS far over the 1 MiB limit, without it 256 KiB.
    max_request_bytes_ = std::size_t{xcb_get_maximum_request_length(conn_)} * 4;
    xcb_flush(conn_);
  }

  xcb_atom_t atom(const char* name) {
    auto r = reply(xcb_intern_atom_reply(conn_, xcb_intern_atom(conn_, 0, std::strlen(name), name), nullptr));
    return r ? r->atom : XCB_ATOM_NONE;
  }

  void run() {
    connect();
    for (;;) {
      run_jobs();
      if (conn_) {
        handle_events();
      }
      bool stopping = false;
      {
        std::lock_guard lock(mutex_);
        stopping = stopping_;
      }
      if (stopping) {
        break;
      }
      wait(std::chrono::milliseconds(200));
      expire_read();
    }
    hand_off();
    if (conn_) {
      xcb_disconnect(conn_);
      conn_ = nullptr;
    }
  }

  void run_jobs() {
    std::deque<std::function<void()>> jobs;
    {
      std::lock_guard lock(mutex_);
      jobs.swap(jobs_);
    }
    for (auto& job : jobs) {
      job();
    }
  }

  void wait(std::chrono::milliseconds timeout) {
    std::array<pollfd, 2> fds{pollfd{wake_, POLLIN, 0}, pollfd{conn_ ? xcb_get_file_descriptor(conn_) : -1, POLLIN, 0}};
    ::poll(fds.data(), conn_ ? 2 : 1, static_cast<int>(timeout.count()));
    std::uint64_t n = 0;
    [[maybe_unused]] auto r = ::read(wake_, &n, sizeof n);
  }

  void handle_events() {
    while (xcb_generic_event_t* raw = xcb_poll_for_event(conn_)) {
      auto event = reply(raw);
      switch (event->response_type & ~0x80) {
        case XCB_SELECTION_REQUEST:
          serve(*reinterpret_cast<xcb_selection_request_event_t*>(event.get()));
          break;
        case XCB_SELECTION_CLEAR: {
          const auto& clear = *reinterpret_cast<xcb_selection_clear_event_t*>(event.get());
          if (clear.selection == atoms_.clipboard) {
            owned_.reset();  // another program has the clipboard now
          }
          break;
        }
        case XCB_SELECTION_NOTIFY:
          selection_notified(*reinterpret_cast<xcb_selection_notify_event_t*>(event.get()));
          break;
        case XCB_PROPERTY_NOTIFY:
          property_changed(*reinterpret_cast<xcb_property_notify_event_t*>(event.get()));
          break;
        default:
          break;  // errors (response_type 0) included: a requestor that went away, say
      }
    }
    xcb_flush(conn_);
  }

  // ----------------------------------------------------------------------------------- writing

  void property_changed(const xcb_property_notify_event_t& e) {
    if (e.window != window_) {
      return;
    }
    if (e.atom == atoms_.time && !pending_writes_.empty()) {
      PendingWrite w = std::move(pending_writes_.front());
      pending_writes_.pop_front();
      xcb_set_selection_owner(conn_, window_, atoms_.clipboard, e.time);
      auto owner = reply(xcb_get_selection_owner_reply(conn_, xcb_get_selection_owner(conn_, atoms_.clipboard), nullptr));
      if (!owner || owner->owner != window_) {
        w.done(failed("UNAVAILABLE", "another program kept the clipboard"));
        return;
      }
      owned_ = std::move(w.text);
      owned_since_ = e.time;
      w.done(Result{});
    } else if (e.atom == atoms_.property && e.state == XCB_PROPERTY_NEW_VALUE && incr_) {
      incr_chunk();
    }
  }

  /// Another program asks for the clipboard we own (ICCCM 2.2).
  void serve(const xcb_selection_request_event_t& r) {
    xcb_selection_notify_event_t answer{};
    answer.response_type = XCB_SELECTION_NOTIFY;
    answer.time = r.time;
    answer.requestor = r.requestor;
    answer.selection = r.selection;
    answer.target = r.target;
    answer.property = XCB_ATOM_NONE;
    const xcb_atom_t property = r.property == XCB_ATOM_NONE ? r.target : r.property;  // obsolete clients
    if (r.selection == atoms_.clipboard && owned_) {
      if (r.target == atoms_.targets) {
        const std::array<xcb_atom_t, 5> targets{atoms_.targets, atoms_.timestamp, atoms_.utf8, atoms_.text, XCB_ATOM_STRING};
        xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, r.requestor, property, XCB_ATOM_ATOM, 32, targets.size(),
                            targets.data());
        answer.property = property;
      } else if (r.target == atoms_.timestamp) {
        xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, r.requestor, property, XCB_ATOM_INTEGER, 32, 1, &owned_since_);
        answer.property = property;
      } else if (r.target == atoms_.utf8 || r.target == atoms_.text) {
        xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, r.requestor, property, atoms_.utf8, 8, owned_->size(),
                            owned_->data());
        answer.property = property;
      } else if (r.target == XCB_ATOM_STRING) {
        const std::string latin1 = to_latin1(*owned_);
        xcb_change_property(conn_, XCB_PROP_MODE_REPLACE, r.requestor, property, XCB_ATOM_STRING, 8, latin1.size(),
                            latin1.data());
        answer.property = property;
      }
    }
    xcb_send_event(conn_, 0, r.requestor, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char*>(&answer));
  }

  /// STRING is ISO Latin-1: what it can't say becomes '?'.
  static std::string to_latin1(const std::string& utf8) {
    std::string out;
    for (std::size_t i = 0; i < utf8.size();) {
      const auto c = static_cast<unsigned char>(utf8[i]);
      if (c < 0x80) {
        out.push_back(static_cast<char>(c));
        i += 1;
      } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
        const unsigned code = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu);
        out.push_back(code <= 0xFF ? static_cast<char>(code) : '?');
        i += 2;
      } else {
        out.push_back('?');
        i += (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
      }
    }
    return out;
  }

  // ----------------------------------------------------------------------------------- reading

  void start_read() {
    read_deadline_ = Clock::now() + kReadTimeout;
    incr_ = false;
    incoming_.clear();
    xcb_convert_selection(conn_, window_, atoms_.clipboard, atoms_.utf8, atoms_.property, XCB_CURRENT_TIME);
    xcb_flush(conn_);
  }

  void finish_read(Result result) {
    if (reads_.empty()) {
      return;
    }
    Done done = std::move(reads_.front());
    reads_.pop_front();
    incr_ = false;
    incoming_.clear();
    done(std::move(result));
    if (!reads_.empty()) {
      start_read();
    }
  }

  void expire_read() {
    if (!reads_.empty() && Clock::now() > read_deadline_) {
      finish_read(failed("TIMEOUT", "the program that has the clipboard didn't hand it over"));
    }
  }

  void selection_notified(const xcb_selection_notify_event_t& e) {
    if (e.selection == atoms_.manager) {
      handed_off_ = true;
      return;
    }
    if (reads_.empty() || e.selection != atoms_.clipboard || incr_) {
      return;
    }
    if (e.property == XCB_ATOM_NONE) {
      finish_read(Result{});  // nobody has it, or it isn't text
      return;
    }
    auto r = take_property();
    if (!r) {
      finish_read(failed("UNAVAILABLE", "the clipboard's text couldn't be read"));
    } else if (r->type == atoms_.incr) {
      // Too big for one property: it comes in chunks, each a new value (deleting it asks for the next).
      incr_ = true;
      read_deadline_ = Clock::now() + kReadTimeout;
    } else {
      finish_read(Result{std::string(static_cast<const char*>(xcb_get_property_value(r.get())),
                                     xcb_get_property_value_length(r.get())),
                         {}, {}});
    }
  }

  void incr_chunk() {
    auto r = take_property();
    const int length = r ? xcb_get_property_value_length(r.get()) : 0;
    if (!r || length == 0) {
      finish_read(r ? Result{std::move(incoming_), {}, {}} : failed("UNAVAILABLE", "the clipboard's text couldn't be read"));
      return;
    }
    incoming_.append(static_cast<const char*>(xcb_get_property_value(r.get())), length);
    read_deadline_ = Clock::now() + kReadTimeout;
    if (incoming_.size() > kMaxText) {
      finish_read(failed("TOO_LARGE", "the clipboard's text is over the 1 MiB limit"));
    }
  }

  /// Our property's value, deleted as it's read.
  Reply<xcb_get_property_reply_t> take_property() {
    return reply(xcb_get_property_reply(
        conn_, xcb_get_property(conn_, 1, window_, atoms_.property, XCB_GET_PROPERTY_TYPE_ANY, 0, kMaxText / 4 + 1),
        nullptr));
  }

  // ------------------------------------------------------------------------------------ at exit

  /// ICCCM's clipboard manager protocol: asked to SAVE_TARGETS, the manager takes the text.
  void hand_off() {
    if (!conn_ || !owned_) {
      return;
    }
    auto manager = reply(xcb_get_selection_owner_reply(conn_, xcb_get_selection_owner(conn_, atoms_.manager), nullptr));
    if (!manager || manager->owner == XCB_WINDOW_NONE) {
      return;
    }
    xcb_convert_selection(conn_, window_, atoms_.manager, atoms_.save_targets, atoms_.property, XCB_CURRENT_TIME);
    xcb_flush(conn_);
    const auto deadline = Clock::now() + kHandOffTimeout;
    while (!handed_off_ && Clock::now() < deadline) {
      handle_events();
      wait(std::chrono::milliseconds(50));
    }
  }

  std::mutex mutex_;
  std::deque<std::function<void()>> jobs_;
  std::thread thread_;
  bool started_ = false;
  bool stopping_ = false;
  const int wake_;

  xcb_connection_t* conn_ = nullptr;
  xcb_window_t window_ = XCB_WINDOW_NONE;
  Atoms atoms_{};
  std::size_t max_request_bytes_ = 0;
  std::optional<std::string> owned_;
  xcb_timestamp_t owned_since_ = XCB_CURRENT_TIME;
  std::deque<PendingWrite> pending_writes_;
  std::deque<Done> reads_;
  Clock::time_point read_deadline_{};
  bool incr_ = false;
  std::string incoming_;
  bool handed_off_ = false;
};

X11& x11() {
  static X11 instance;
  return instance;
}

}  // namespace

void write_text(std::string text, Done done) {
  x11().post([text = std::move(text), done = std::move(done)]() mutable { x11().write(std::move(text), done); });
}

void read_text(Done done) {
  x11().post([done = std::move(done)] { x11().read(done); });
}

void shutdown() { x11().stop(); }

}  // namespace pico_desktop::host::cef::clipboard
