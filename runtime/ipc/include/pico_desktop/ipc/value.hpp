// Payloads the host itself reads (the backend's `host.*` service requests): a CBOR item decoded
// into a tree. Only for small, profile-checked payloads; routing never decodes payloads.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pico_desktop::ipc {

class Value {
 public:
  enum class Type : std::uint8_t { uint, negative, bytes, text, array, map, boolean, null, number };

  Type type = Type::null;
  std::uint64_t integer = 0;  // uint: the value; negative: −1 − the value
  double number = 0;          // a float
  bool flag = false;          // boolean
  std::string text;           // text (bytes: the raw bytes)
  std::vector<Value> items;   // array, or a map's values
  std::vector<std::string> keys;  // a map's (text) keys, in order

  /// A map's member, if this is a map and it has one (null members count as absent).
  const Value* get(std::string_view key) const;
  std::optional<std::string> as_text() const;
  std::optional<std::uint64_t> as_uint() const;
  std::optional<bool> as_bool() const;
  bool is_null() const { return type == Type::null; }
};

/// `bytes` as one value: nothing unless it's exactly one item that follows the CBOR profile, with
/// text map keys only (payload maps).
std::optional<Value> decode_value(std::span<const std::byte> bytes);

}  // namespace pico_desktop::ipc
