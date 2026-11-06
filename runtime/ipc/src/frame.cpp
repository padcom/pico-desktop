#include "pico_desktop/ipc/frame.hpp"

namespace pico_desktop::ipc {

std::array<std::byte, frame_header_bytes> encode_frame_header(std::uint32_t length) {
  return {
    static_cast<std::byte>(length >> 24),
    static_cast<std::byte>(length >> 16),
    static_cast<std::byte>(length >> 8),
    static_cast<std::byte>(length),
  };
}

std::optional<std::uint32_t> decode_frame_header(std::span<const std::byte> bytes) {
  if (bytes.size() < frame_header_bytes) {
    return std::nullopt;
  }
  std::uint32_t length = 0;
  for (std::size_t i = 0; i < frame_header_bytes; ++i) {
    length = (length << 8) | std::to_integer<std::uint32_t>(bytes[i]);
  }
  return length;
}

}  // namespace pico_desktop::ipc
