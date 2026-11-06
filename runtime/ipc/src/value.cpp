#include "pico_desktop/ipc/value.hpp"

#include <bit>
#include <cmath>
#include <cstring>

#include "pico_desktop/ipc/cbor.hpp"

namespace pico_desktop::ipc {

namespace {

class Decoder {
 public:
  explicit Decoder(std::span<const std::byte> bytes) : bytes_(bytes) {}

  std::optional<Value> item() {
    const auto first = next();
    if (!first) {
      return std::nullopt;
    }
    const auto major = static_cast<std::uint8_t>(*first >> 5);
    const auto info = static_cast<std::uint8_t>(*first & 0x1f);
    if (major == 7) {
      return simple(info);
    }
    const auto n = argument(info);
    if (!n) {
      return std::nullopt;
    }
    switch (major) {
      case 0:
        return number_value(Value::Type::uint, *n);
      case 1:
        return number_value(Value::Type::negative, *n);
      case 2:
      case 3:
        return string_value(major == 2 ? Value::Type::bytes : Value::Type::text, *n);
      case 4:
        return array(*n);
      case 5:
        return map(*n);
      default:
        return std::nullopt;  // tags aren't in the profile
    }
  }

  bool done() const { return at_ == bytes_.size(); }

 private:
  std::optional<std::uint8_t> next() {
    if (at_ >= bytes_.size()) {
      return std::nullopt;
    }
    return static_cast<std::uint8_t>(bytes_[at_++]);
  }

  std::optional<std::uint64_t> big_endian(std::size_t size) {
    if (bytes_.size() - at_ < size) {
      return std::nullopt;
    }
    std::uint64_t n = 0;
    for (std::size_t i = 0; i < size; ++i) {
      n = (n << 8) | static_cast<std::uint8_t>(bytes_[at_++]);
    }
    return n;
  }

  std::optional<std::uint64_t> argument(std::uint8_t info) {
    if (info < 24) {
      return info;
    }
    if (info > 27) {
      return std::nullopt;  // indefinite lengths aren't in the profile
    }
    return big_endian(std::size_t{1} << (info - 24));
  }

  static Value number_value(Value::Type type, std::uint64_t n) {
    Value v;
    v.type = type;
    v.integer = n;
    return v;
  }

  std::optional<Value> string_value(Value::Type type, std::uint64_t size) {
    if (bytes_.size() - at_ < size) {
      return std::nullopt;
    }
    Value v;
    v.type = type;
    v.text.assign(reinterpret_cast<const char*>(bytes_.data() + at_), size);
    at_ += size;
    return v;
  }

  std::optional<Value> array(std::uint64_t count) {
    Value v;
    v.type = Value::Type::array;
    for (std::uint64_t i = 0; i < count; ++i) {
      auto item = this->item();
      if (!item) {
        return std::nullopt;
      }
      v.items.push_back(std::move(*item));
    }
    return v;
  }

  std::optional<Value> map(std::uint64_t count) {
    Value v;
    v.type = Value::Type::map;
    for (std::uint64_t i = 0; i < count; ++i) {
      auto key = item();
      auto value = key && key->type == Value::Type::text ? item() : std::nullopt;
      if (!value) {
        return std::nullopt;
      }
      v.keys.push_back(std::move(key->text));
      v.items.push_back(std::move(*value));
    }
    return v;
  }

  std::optional<Value> simple(std::uint8_t info) {
    Value v;
    switch (info) {
      case 20:
      case 21:
        v.type = Value::Type::boolean;
        v.flag = info == 21;
        return v;
      case 22:
        return v;  // null
      case 25:
        return half();
      case 26:
        return single();
      case 27:
        return float64();
      default:
        return std::nullopt;
    }
  }

  std::optional<Value> float64() {
    const auto bits = big_endian(8);
    if (!bits) {
      return std::nullopt;
    }
    Value v;
    v.type = Value::Type::number;
    v.number = std::bit_cast<double>(*bits);
    return v;
  }

  std::optional<Value> single() {
    const auto bits = big_endian(4);
    if (!bits) {
      return std::nullopt;
    }
    Value v;
    v.type = Value::Type::number;
    v.number = std::bit_cast<float>(static_cast<std::uint32_t>(*bits));
    return v;
  }

  std::optional<Value> half() {
    const auto bits = big_endian(2);
    if (!bits) {
      return std::nullopt;
    }
    const auto h = static_cast<std::uint16_t>(*bits);
    const int exponent = (h >> 10) & 0x1f;
    const double mantissa = h & 0x3ff;
    double value = exponent == 0    ? std::ldexp(mantissa, -24)
                   : exponent == 31 ? (mantissa == 0 ? INFINITY : NAN)
                                    : std::ldexp(mantissa + 1024, exponent - 25);
    Value v;
    v.type = Value::Type::number;
    v.number = (h & 0x8000) ? -value : value;
    return v;
  }

  std::span<const std::byte> bytes_;
  std::size_t at_ = 0;
};

}  // namespace

const Value* Value::get(std::string_view key) const {
  if (type != Type::map) {
    return nullptr;
  }
  for (std::size_t i = 0; i < keys.size(); ++i) {
    if (keys[i] == key) {
      return items[i].is_null() ? nullptr : &items[i];
    }
  }
  return nullptr;
}

std::optional<std::string> Value::as_text() const {
  return type == Type::text ? std::optional<std::string>(text) : std::nullopt;
}

std::optional<std::uint64_t> Value::as_uint() const {
  return type == Type::uint ? std::optional<std::uint64_t>(integer) : std::nullopt;
}

std::optional<bool> Value::as_bool() const {
  return type == Type::boolean ? std::optional<bool>(flag) : std::nullopt;
}

std::optional<Value> decode_value(std::span<const std::byte> bytes) {
  if (check_profile(bytes)) {
    return std::nullopt;
  }
  Decoder d(bytes);
  auto value = d.item();
  return value && d.done() ? value : std::nullopt;
}

}  // namespace pico_desktop::ipc
