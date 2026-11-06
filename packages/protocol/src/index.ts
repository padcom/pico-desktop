/**
 * The Pico Desktop wire protocol: what both Communicators and the host agree on.
 * The envelope's CBOR encoding is specified in F2 (docs/adr/0002-cbor-rpc-event-model.md); this
 * package holds the parts that are already decided.
 */

/** The protocol version sent in the compatibility handshake (0 until F2 freezes the envelope). */
export const PROTOCOL_VERSION = 0

/** Envelope kinds (PROJECT_DESCRIPTION.md §5). */
export const MESSAGE_KINDS = [
  'request',
  'response',
  'error',
  'event',
  'subscribe',
  'unsubscribe',
  'cancel',
  'lifecycle',
] as const

export type MessageKind = typeof MESSAGE_KINDS[number]

/** Byte-stream transports frame each message as a u32 big-endian payload length, then the payload. */
export const FRAME_HEADER_BYTES = 4

export function isMessageKind(value: string): value is MessageKind {
  return (MESSAGE_KINDS as readonly string[]).includes(value)
}
