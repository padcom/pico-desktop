/**
 * The Pico Desktop wire protocol (docs/protocol.md): the CBOR profile, envelopes and framing,
 * shared by the TypeScript Communicator and the tools.
 */
export { type CborMap, type CborObject, type CborValue, CborError, MAX_DEPTH, decode, encode } from './cbor'

export {
  type Envelope,
  type ErrorCode,
  type ErrorInfo,
  type MessageKind,
  ERROR_CODES,
  MAX_NAME_BYTES,
  MESSAGE_KINDS,
  PROTOCOL_VERSION,
  ProtocolError,
  decodeEnvelope,
  encodeEnvelope,
  isMessageKind,
} from './envelope'

export {
  ABSOLUTE_MAX_MESSAGE_BYTES,
  DEFAULT_MAX_MESSAGE_BYTES,
  FRAME_HEADER_BYTES,
  FrameDecoder,
  encodeFrame,
} from './framing'
