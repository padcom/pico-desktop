// The host's view of messages (docs/protocol.md): it checks that a message follows the CBOR
// profile, reads the envelope fields it routes by, and adds or strips the window field, without
// interpreting payloads. No exceptions (the CEF host builds with -fno-exceptions).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pico_desktop::ipc {

inline constexpr std::uint64_t kProtocolVersion = 1;
inline constexpr std::uint64_t kMaxId = 9007199254740991ull;  // 2^53 − 1
inline constexpr std::size_t kMaxDepth = 32;
inline constexpr std::size_t kMaxNameBytes = 256;

enum class Kind : std::uint8_t { request, response, error, event, subscribe, unsubscribe, cancel, lifecycle };

/// The envelope fields the host routes by (payload and error stay opaque bytes).
struct EnvelopeHeader {
  Kind kind = Kind::request;
  std::optional<std::uint64_t> id;
  std::optional<std::string> name;
  std::optional<std::uint64_t> window;
  std::optional<std::uint64_t> epoch;
  std::optional<std::uint64_t> subscription;
};

/// Why a message was refused: a protocol error code and a description.
struct Refusal {
  std::string code;     // INVALID_MESSAGE, UNSUPPORTED_VERSION
  std::string message;
};

/// The outcome of reading an envelope: the header, or why it was refused.
struct ReadResult {
  std::optional<EnvelopeHeader> header;
  Refusal refusal;
  explicit operator bool() const { return header.has_value(); }
};

/// Nothing when `bytes` is exactly one CBOR item that follows the profile, else what's wrong.
std::optional<std::string> check_profile(std::span<const std::byte> bytes);

/// Checks the profile and the envelope rules, and returns the routing fields.
ReadResult read_envelope(std::span<const std::byte> bytes);

/// `envelope` with its window field set to `window` (it must have none: frontends can't set it).
std::vector<std::byte> with_window(std::span<const std::byte> envelope, std::uint64_t window);

/// `envelope` without its window field (what the backend's messages look like to a frontend).
std::vector<std::byte> without_window(std::span<const std::byte> envelope);

/// The raw CBOR of uint-keyed field `key` of an envelope (for example its payload), if present.
std::optional<std::vector<std::byte>> raw_field(std::span<const std::byte> envelope, std::uint64_t key);

/// Envelope keys the host rewrites when routing.
inline constexpr std::uint64_t kKeyId = 2;
inline constexpr std::uint64_t kKeyWindow = 6;
inline constexpr std::uint64_t kKeyDropped = 9;

/// The value of uint-keyed field `key` of an envelope, if it has one and it is an unsigned integer.
std::optional<std::uint64_t> read_uint_field(std::span<const std::byte> envelope, std::uint64_t key);

/// `envelope` with uint field `key` set to `value` (replacing it, or added at the end), or removed
/// when `value` is empty. Empty on an invalid envelope.
std::vector<std::byte> with_field(std::span<const std::byte> envelope, std::uint64_t key,
                                  std::optional<std::uint64_t> value);

/// A minimal CBOR writer for the messages the host originates (lifecycle, errors) and for tests.
class Writer {
 public:
  Writer& uint(std::uint64_t n);
  Writer& negative(std::uint64_t n);  // the integer −1 − n
  Writer& text(std::string_view s);
  Writer& bytes(std::span<const std::byte> b);
  Writer& array(std::size_t items);
  Writer& map(std::size_t entries);
  Writer& boolean(bool b);
  Writer& null();
  /// Appends an already encoded CBOR item.
  Writer& raw(std::span<const std::byte> item);
  const std::vector<std::byte>& data() const { return out_; }
  std::vector<std::byte> take() { return std::move(out_); }

 private:
  void head(std::uint8_t major, std::uint64_t n);
  std::vector<std::byte> out_;
};

/// Hex helpers (tests, traces).
std::vector<std::byte> from_hex(std::string_view hex);
std::string to_hex(std::span<const std::byte> bytes);

}  // namespace pico_desktop::ipc
