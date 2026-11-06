#include "pico_desktop/ipc/cbor.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace pico_desktop::ipc {

namespace {

struct Range {
  std::size_t begin = 0;
  std::size_t end = 0;
};

struct Entry {
  Range key;
  Range value;
};

/// Walks CBOR items, checking the profile without building values.
class Walker {
 public:
  explicit Walker(std::span<const std::byte> bytes) : b_(bytes) {}

  std::size_t pos() const { return pos_; }
  const std::string& error() const { return error_; }
  bool at_end() const { return pos_ == b_.size(); }

  /// Walks one item starting at the current position.
  bool item(std::size_t depth) {
    std::uint8_t major = 0;
    std::uint8_t info = 0;
    std::uint64_t argument = 0;
    if (!head(major, info, argument)) {
      return false;
    }
    switch (major) {
      case 0:
      case 1:
        return true;
      case 2:
        return skip(argument);
      case 3:
        return text(argument);
      case 4:
        return array(argument, depth);
      case 5:
        return map(argument, depth, nullptr);
      case 6:
        return fail("tags are not allowed");
      default:
        return simple(info);
    }
  }

  /// Walks a map's entries at the current position, recording their ranges.
  bool map_entries(std::vector<Entry>& entries) {
    std::uint8_t major = 0;
    std::uint8_t info = 0;
    std::uint64_t argument = 0;
    if (!head(major, info, argument)) {
      return false;
    }
    if (major != 5) {
      return fail("an envelope is a map with integer keys");
    }
    return map(argument, 1, &entries);
  }

  /// Reads a head; `argument` is the length or value. Only definite lengths.
  bool head(std::uint8_t& major, std::uint8_t& info, std::uint64_t& argument) {
    if (pos_ >= b_.size()) {
      return fail("truncated");
    }
    const auto initial = std::to_integer<std::uint8_t>(b_[pos_++]);
    major = initial >> 5;
    info = initial & 0x1f;
    if (info < 24) {
      argument = info;
      return true;
    }
    if (major == 7 && info >= 25 && info <= 27) {
      argument = 0;  // floats: the bytes follow, `simple` reads them
      return true;
    }
    if (info > 27) {
      return fail(info == 31 ? "indefinite lengths are not allowed" : "reserved additional information");
    }
    const std::size_t size = std::size_t{1} << (info - 24);
    if (b_.size() - pos_ < size) {
      return fail("truncated");
    }
    argument = 0;
    for (std::size_t i = 0; i < size; ++i) {
      argument = (argument << 8) | std::to_integer<std::uint64_t>(b_[pos_++]);
    }
    return true;
  }

  bool fail(const char* message) {
    if (error_.empty()) {
      error_ = message;
    }
    return false;
  }

 private:
  bool skip(std::uint64_t n) {
    if (n > b_.size() - pos_) {
      return fail("a length beyond the end of the message");
    }
    pos_ += static_cast<std::size_t>(n);
    return true;
  }

  bool text(std::uint64_t n) {
    const std::size_t begin = pos_;
    if (!skip(n)) {
      return false;
    }
    return valid_utf8(b_.subspan(begin, pos_ - begin)) || fail("text is not valid UTF-8");
  }

  bool array(std::uint64_t n, std::size_t depth) {
    if (depth > kMaxDepth) {
      return fail("nesting deeper than 32");
    }
    if (n > b_.size() - pos_) {
      return fail("a length beyond the end of the message");
    }
    for (std::uint64_t i = 0; i < n; ++i) {
      if (!item(depth + 1)) {
        return false;
      }
    }
    return true;
  }

  struct Key {
    bool is_text = false;
    std::uint64_t value = 0;
    Range bytes;
  };

  bool key(Key& out) {
    const std::size_t begin = pos_;
    std::uint8_t major = 0;
    std::uint8_t info = 0;
    std::uint64_t argument = 0;
    if (!head(major, info, argument)) {
      return false;
    }
    if (major == 0) {
      out = {false, argument, {begin, pos_}};
      return true;
    }
    if (major != 3) {
      return fail("map keys must be text or unsigned integers");
    }
    const std::size_t text_begin = pos_;
    if (!text(argument)) {
      return false;
    }
    out = {true, 0, {text_begin, pos_}};
    return true;
  }

  bool same(const Key& a, const Key& b) const {
    if (a.is_text != b.is_text) {
      return false;
    }
    if (!a.is_text) {
      return a.value == b.value;
    }
    return std::equal(b_.begin() + a.bytes.begin, b_.begin() + a.bytes.end, b_.begin() + b.bytes.begin,
                      b_.begin() + b.bytes.end);
  }

  bool map(std::uint64_t n, std::size_t depth, std::vector<Entry>* entries) {
    if (depth > kMaxDepth) {
      return fail("nesting deeper than 32");
    }
    if (n > (b_.size() - pos_) / 2) {
      return fail("a length beyond the end of the message");
    }
    std::vector<Key> keys;
    keys.reserve(static_cast<std::size_t>(n));
    for (std::uint64_t i = 0; i < n; ++i) {
      const std::size_t key_begin = pos_;
      Key k;
      if (!key(k)) {
        return false;
      }
      for (const Key& seen : keys) {
        if (same(seen, k)) {
          return fail("duplicate map key");
        }
        if (seen.is_text != k.is_text) {
          return fail("a map mixes text and integer keys");
        }
      }
      keys.push_back(k);
      const std::size_t value_begin = pos_;
      if (!item(depth + 1)) {
        return false;
      }
      if (entries) {
        entries->push_back({{key_begin, value_begin}, {value_begin, pos_}});
      }
      if (entries && k.is_text) {
        return fail("an envelope is a map with integer keys");
      }
    }
    return true;
  }

  bool simple(std::uint8_t info) {
    if (info >= 20 && info <= 22) {
      return true;
    }
    if (info >= 25 && info <= 27) {
      return skip(std::size_t{1} << (info - 24));
    }
    return fail(info == 23 ? "undefined is not allowed" : "simple values other than false, true and null are not allowed");
  }

  static bool valid_utf8(std::span<const std::byte> s) {
    std::size_t i = 0;
    while (i < s.size()) {
      const auto c = std::to_integer<std::uint8_t>(s[i]);
      std::size_t extra = 0;
      std::uint32_t code = 0;
      std::uint32_t minimum = 0;
      if (c < 0x80) {
        ++i;
        continue;
      }
      if ((c & 0xe0) == 0xc0) {
        extra = 1, code = c & 0x1f, minimum = 0x80;
      } else if ((c & 0xf0) == 0xe0) {
        extra = 2, code = c & 0x0f, minimum = 0x800;
      } else if ((c & 0xf8) == 0xf0) {
        extra = 3, code = c & 0x07, minimum = 0x10000;
      } else {
        return false;
      }
      if (i + extra >= s.size()) {  // the continuation bytes must be there
        return false;
      }
      for (std::size_t k = 1; k <= extra; ++k) {
        const auto cc = std::to_integer<std::uint8_t>(s[i + k]);
        if ((cc & 0xc0) != 0x80) {
          return false;
        }
        code = (code << 6) | (cc & 0x3f);
      }
      if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
        return false;
      }
      i += extra + 1;
    }
    return true;
  }

  std::span<const std::byte> b_;
  std::size_t pos_ = 0;
  std::string error_;
};

std::optional<std::uint64_t> read_uint(std::span<const std::byte> item) {
  Walker w(item);
  std::uint8_t major = 0;
  std::uint8_t info = 0;
  std::uint64_t argument = 0;
  if (!w.head(major, info, argument) || major != 0) {
    return std::nullopt;
  }
  return argument;
}

std::optional<std::string> read_text(std::span<const std::byte> item) {
  Walker w(item);
  std::uint8_t major = 0;
  std::uint8_t info = 0;
  std::uint64_t argument = 0;
  if (!w.head(major, info, argument) || major != 3) {
    return std::nullopt;
  }
  const auto* data = reinterpret_cast<const char*>(item.data()) + w.pos();
  return std::string(data, static_cast<std::size_t>(argument));
}

/// The value of uint key `key` in `entries`, if any.
std::optional<std::span<const std::byte>> field(std::span<const std::byte> bytes, const std::vector<Entry>& entries,
                                                std::uint64_t key) {
  for (const Entry& e : entries) {
    if (read_uint(bytes.subspan(e.key.begin, e.key.end - e.key.begin)) == key) {
      return bytes.subspan(e.value.begin, e.value.end - e.value.begin);
    }
  }
  return std::nullopt;
}

ReadResult refuse(const char* code, std::string message) {
  return {std::nullopt, {code, std::move(message)}};
}

/// Checks an optional uint field; false (with `refusal` set) when it's there but invalid.
bool uint_field(std::span<const std::byte> bytes, const std::vector<Entry>& entries, std::uint64_t key,
                const char* name, std::optional<std::uint64_t>& out, ReadResult& refusal) {
  const auto value = field(bytes, entries, key);
  if (!value) {
    return true;
  }
  out = read_uint(*value);
  if (!out || *out > kMaxId) {
    refusal = refuse("INVALID_MESSAGE", std::string(name) + " must be an unsigned integer below 2^53");
    return false;
  }
  return true;
}

bool error_field(std::span<const std::byte> value) {
  Walker w(value);
  std::vector<Entry> entries;
  if (!w.map_entries(entries)) {
    return false;
  }
  const auto code = field(value, entries, 0);
  const auto message = field(value, entries, 1);
  return code && message && read_text(*code) && read_text(*message);
}

bool required(const EnvelopeHeader& h, bool has_error, std::string& missing) {
  const bool needs_id = h.kind == Kind::request || h.kind == Kind::response || h.kind == Kind::error ||
                        h.kind == Kind::subscribe || h.kind == Kind::cancel;
  const bool needs_name = h.kind == Kind::request || h.kind == Kind::event || h.kind == Kind::subscribe ||
                          h.kind == Kind::lifecycle;
  if (needs_id && !h.id) {
    missing = "id";
  } else if (needs_name && !h.name) {
    missing = "n";
  } else if (h.kind == Kind::error && !has_error) {
    missing = "err";
  } else if (h.kind == Kind::unsubscribe && !h.subscription) {
    missing = "s";
  }
  return missing.empty();
}

/// The map's entries, or nothing (after setting `refusal`) when it isn't a valid envelope map.
std::optional<std::vector<Entry>> envelope_entries(std::span<const std::byte> bytes, ReadResult& refusal) {
  if (const auto problem = check_profile(bytes)) {
    refusal = refuse("INVALID_MESSAGE", *problem);
    return std::nullopt;
  }
  Walker w(bytes);
  std::vector<Entry> entries;
  if (!w.map_entries(entries) || entries.empty()) {
    refusal = refuse("INVALID_MESSAGE", "an envelope is a map with integer keys");
    return std::nullopt;
  }
  return entries;
}

std::vector<std::byte> rebuild(std::span<const std::byte> bytes, const std::vector<Entry>& entries,
                               std::optional<std::uint64_t> drop_key, std::optional<std::uint64_t> add_window) {
  std::size_t count = 0;
  for (const Entry& e : entries) {
    count += read_uint(bytes.subspan(e.key.begin, e.key.end - e.key.begin)) == drop_key ? 0 : 1;
  }
  Writer w;
  w.map(count + (add_window ? 1 : 0));
  for (const Entry& e : entries) {
    if (drop_key && read_uint(bytes.subspan(e.key.begin, e.key.end - e.key.begin)) == drop_key) {
      continue;
    }
    w.raw(bytes.subspan(e.key.begin, e.value.end - e.key.begin));
  }
  if (add_window) {
    w.uint(6).uint(*add_window);
  }
  return w.take();
}

}  // namespace

std::optional<std::string> check_profile(std::span<const std::byte> bytes) {
  Walker w(bytes);
  if (!w.item(1)) {
    return w.error();
  }
  if (!w.at_end()) {
    return std::string("trailing bytes after the item");
  }
  return std::nullopt;
}

ReadResult read_envelope(std::span<const std::byte> bytes) {
  ReadResult result;
  const auto entries = envelope_entries(bytes, result);
  if (!entries) {
    return result;
  }
  const auto version = field(bytes, *entries, 0);
  const auto v = version ? read_uint(*version) : std::nullopt;
  if (!v) {
    return refuse("INVALID_MESSAGE", version ? "v must be an unsigned integer" : "missing v");
  }
  if (*v != kProtocolVersion) {
    return refuse("UNSUPPORTED_VERSION", "protocol version " + std::to_string(*v) + " (this side speaks 1)");
  }
  const auto kind_item = field(bytes, *entries, 1);
  const auto kind = kind_item ? read_uint(*kind_item) : std::nullopt;
  if (!kind || *kind > static_cast<std::uint64_t>(Kind::lifecycle)) {
    return refuse("INVALID_MESSAGE", "unknown kind");
  }
  EnvelopeHeader h;
  h.kind = static_cast<Kind>(*kind);
  std::optional<std::uint64_t> ignored;
  if (!uint_field(bytes, *entries, 2, "id", h.id, result) || !uint_field(bytes, *entries, 6, "w", h.window, result) ||
      !uint_field(bytes, *entries, 7, "e", h.epoch, result) ||
      !uint_field(bytes, *entries, 8, "s", h.subscription, result) ||
      !uint_field(bytes, *entries, 9, "d", ignored, result)) {
    return result;
  }
  if (const auto name = field(bytes, *entries, 3)) {
    h.name = read_text(*name);
    if (!h.name) {
      return refuse("INVALID_MESSAGE", "n must be text");
    }
  }
  const auto err = field(bytes, *entries, 5);
  if (err && !error_field(*err)) {
    return refuse("INVALID_MESSAGE", "err must be a map with a code and a message");
  }
  std::string missing;
  if (!required(h, err.has_value(), missing)) {
    return refuse("INVALID_MESSAGE", "the message needs " + missing);
  }
  if (h.name && h.name->size() > kMaxNameBytes) {
    return refuse("INVALID_MESSAGE", "names are at most 256 bytes");
  }
  result.header = std::move(h);
  return result;
}

std::vector<std::byte> with_window(std::span<const std::byte> envelope, std::uint64_t window) {
  ReadResult result;
  const auto entries = envelope_entries(envelope, result);
  if (!entries || field(envelope, *entries, 6)) {
    return {};
  }
  return rebuild(envelope, *entries, std::nullopt, window);
}

std::vector<std::byte> without_window(std::span<const std::byte> envelope) {
  ReadResult result;
  const auto entries = envelope_entries(envelope, result);
  if (!entries) {
    return {};
  }
  return rebuild(envelope, *entries, std::uint64_t{6}, std::nullopt);
}

void Writer::head(std::uint8_t major, std::uint64_t n) {
  const auto m = static_cast<std::uint8_t>(major << 5);
  std::size_t size = 0;
  std::uint8_t info = 0;
  if (n < 24) {
    out_.push_back(static_cast<std::byte>(m | n));
    return;
  }
  if (n < 0x100) {
    size = 1, info = 24;
  } else if (n < 0x10000) {
    size = 2, info = 25;
  } else if (n < 0x100000000ull) {
    size = 4, info = 26;
  } else {
    size = 8, info = 27;
  }
  out_.push_back(static_cast<std::byte>(m | info));
  for (std::size_t i = size; i-- > 0;) {
    out_.push_back(static_cast<std::byte>((n >> (8 * i)) & 0xff));
  }
}

Writer& Writer::uint(std::uint64_t n) {
  head(0, n);
  return *this;
}

Writer& Writer::negative(std::uint64_t n) {
  head(1, n);
  return *this;
}

Writer& Writer::text(std::string_view s) {
  head(3, s.size());
  const auto* p = reinterpret_cast<const std::byte*>(s.data());
  out_.insert(out_.end(), p, p + s.size());
  return *this;
}

Writer& Writer::bytes(std::span<const std::byte> b) {
  head(2, b.size());
  out_.insert(out_.end(), b.begin(), b.end());
  return *this;
}

Writer& Writer::array(std::size_t items) {
  head(4, items);
  return *this;
}

Writer& Writer::map(std::size_t entries) {
  head(5, entries);
  return *this;
}

Writer& Writer::boolean(bool b) {
  out_.push_back(static_cast<std::byte>(b ? 0xf5 : 0xf4));
  return *this;
}

Writer& Writer::null() {
  out_.push_back(std::byte{0xf6});
  return *this;
}

Writer& Writer::raw(std::span<const std::byte> item) {
  out_.insert(out_.end(), item.begin(), item.end());
  return *this;
}

std::vector<std::byte> from_hex(std::string_view hex) {
  const auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  std::vector<std::byte> out;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    out.push_back(static_cast<std::byte>(nibble(hex[i]) * 16 + nibble(hex[i + 1])));
  }
  return out;
}

std::string to_hex(std::span<const std::byte> bytes) {
  static constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                               '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string out;
  out.reserve(bytes.size() * 2);
  for (const std::byte b : bytes) {
    const auto v = std::to_integer<std::uint8_t>(b);
    out.push_back(digits[v >> 4]);
    out.push_back(digits[v & 15]);
  }
  return out;
}

}  // namespace pico_desktop::ipc
