// Framing for byte-stream transports (stdio pipes, sockets): every message is a u32 big-endian
// payload length followed by the payload. The same layout as FRAME_HEADER_BYTES in
// @pico-desktop/protocol and the Pico Communicator; the transport choice is ADR-0004.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace pico_desktop::ipc {

inline constexpr std::size_t frame_header_bytes = 4;

/// The header announcing a payload of `length` bytes.
std::array<std::byte, frame_header_bytes> encode_frame_header(std::uint32_t length);

/// The payload length announced by the first bytes of `bytes`, or nothing while fewer than
/// `frame_header_bytes` bytes have arrived.
std::optional<std::uint32_t> decode_frame_header(std::span<const std::byte> bytes);

}  // namespace pico_desktop::ipc
