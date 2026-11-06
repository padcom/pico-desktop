// Framing for byte-stream transports (docs/protocol.md §2): a u32 big-endian length, then the
// message. The decoder accepts arbitrary chunking and enforces the size limit before buffering.
import { ProtocolError } from './envelope'

export const FRAME_HEADER_BYTES = 4

export const DEFAULT_MAX_MESSAGE_BYTES = 4 * 1024 * 1024

export const ABSOLUTE_MAX_MESSAGE_BYTES = 16 * 1024 * 1024

/**
 * One frame for `message`.
 *
 * @param message the message bytes (1 … 16 MiB)
 * @returns the length header followed by the message
 */
export function encodeFrame(message: Uint8Array): Uint8Array {
  if (message.length === 0 || message.length > ABSOLUTE_MAX_MESSAGE_BYTES) {
    throw new ProtocolError('MESSAGE_TOO_LARGE', `a frame holds 1 to ${ABSOLUTE_MAX_MESSAGE_BYTES} bytes`)
  }
  const frame = new Uint8Array(FRAME_HEADER_BYTES + message.length)
  new DataView(frame.buffer).setUint32(0, message.length)
  frame.set(message, FRAME_HEADER_BYTES)

  return frame
}

/** Splits a byte stream back into messages. After an error the stream is unusable. */
export class FrameDecoder {
  private buffer = new Uint8Array(0)
  private failed = false

  constructor(private readonly maxMessageBytes = DEFAULT_MAX_MESSAGE_BYTES) {
    if (maxMessageBytes < 1 || maxMessageBytes > ABSOLUTE_MAX_MESSAGE_BYTES) {
      throw new RangeError(`maxMessageBytes must be 1 … ${ABSOLUTE_MAX_MESSAGE_BYTES}`)
    }
  }

  /**
   * Adds received bytes.
   *
   * @param chunk bytes as they arrived
   * @returns the messages completed by them, in order
   * @throws {ProtocolError} on a zero or oversized length (then every later call throws too)
   */
  push(chunk: Uint8Array): Uint8Array[] {
    if (this.failed) {
      throw new ProtocolError('INVALID_MESSAGE', 'the stream already failed')
    }
    const joined = new Uint8Array(this.buffer.length + chunk.length)
    joined.set(this.buffer)
    joined.set(chunk, this.buffer.length)
    const messages: Uint8Array[] = []
    let at = 0
    while (joined.length - at >= FRAME_HEADER_BYTES) {
      const length = new DataView(joined.buffer, at).getUint32(0)
      this.check(length)

      if (joined.length - at - FRAME_HEADER_BYTES < length) {
        break
      }
      messages.push(joined.slice(at + FRAME_HEADER_BYTES, at + FRAME_HEADER_BYTES + length))
      at += FRAME_HEADER_BYTES + length
    }
    this.buffer = joined.slice(at)

    return messages
  }

  private check(length: number) {
    if (length === 0) {
      this.failed = true
      throw new ProtocolError('INVALID_MESSAGE', 'a zero-length frame')
    }
    if (length > this.maxMessageBytes) {
      this.failed = true
      throw new ProtocolError('MESSAGE_TOO_LARGE', `a ${length}-byte frame (the limit is ${this.maxMessageBytes})`)
    }
  }

  /**
   * Bytes of an unfinished frame (a non-zero value at end of stream means it was cut off).
   *
   * @returns how many bytes are buffered
   */
  get pending(): number {
    return this.buffer.length
  }
}
