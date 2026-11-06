// The protocol envelope (docs/protocol.md §4): a uint-keyed CBOR map, validated on decode.
import { type CborMap, type CborValue, CborError, decode, encode } from './cbor'

export const PROTOCOL_VERSION = 1

/** Envelope kinds, in wire order (the index is `k`). */
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

export const ERROR_CODES = [
  'UNKNOWN_METHOD',
  'INVALID_PAYLOAD',
  'INVALID_MESSAGE',
  'MESSAGE_TOO_LARGE',
  'UNSUPPORTED_VERSION',
  'NOT_ALLOWED',
  'DISCONNECTED',
  'TIMEOUT',
  'CANCELLED',
  'BUSY',
  'HANDLER_ERROR',
  'INTERNAL',
] as const

export type ErrorCode = typeof ERROR_CODES[number]

export interface ErrorInfo {
  code: string
  message: string
  data?: CborValue
}

export interface Envelope {
  v: number
  k: MessageKind
  id?: number
  n?: string
  p?: CborValue
  err?: ErrorInfo
  w?: number
  e?: number
  s?: number
  d?: number
}

/** A message that can't be accepted; `code` says why. */
export class ProtocolError extends Error {
  constructor(readonly code: ErrorCode, message: string) {
    super(message)
    this.name = 'ProtocolError'
  }
}

export const MAX_NAME_BYTES = 256

/** Envelope fields in key order (the index is the CBOR key). */
const FIELDS = ['v', 'k', 'id', 'n', 'p', 'err', 'w', 'e', 's', 'd'] as const
const UINT_FIELDS = ['id', 'w', 'e', 's', 'd'] as const

const REQUIRED: Record<MessageKind, readonly (keyof Envelope)[]> = {
  request: ['id', 'n'],
  response: ['id'],
  error: ['id', 'err'],
  event: ['n'],
  subscribe: ['id', 'n'],
  unsubscribe: ['s'],
  cancel: ['id'],
  lifecycle: ['n'],
}

export function isMessageKind(value: string): value is MessageKind {
  return (MESSAGE_KINDS as readonly string[]).includes(value)
}

const invalid = (message: string) => new ProtocolError('INVALID_MESSAGE', message)

const isUint = (value: CborValue): value is number => typeof value === 'number' && Number.isSafeInteger(value) && value >= 0

function uint(value: CborValue | undefined, field: string): number | undefined {
  if (value !== undefined && !isUint(value)) {
    throw invalid(`${field} must be an unsigned integer below 2^53`)
  }

  return value
}

function text(value: CborValue | undefined, field: string): string | undefined {
  if (value !== undefined && typeof value !== 'string') {
    throw invalid(`${field} must be text`)
  }

  return value
}

function withData(info: ErrorInfo, data: CborValue | undefined): ErrorInfo {
  return data === undefined ? info : { ...info, data }
}

function parseError(map: CborMap): ErrorInfo {
  const code = text(map.get(0), 'err.code')
  const message = text(map.get(1), 'err.message')
  if (code === undefined || message === undefined) {
    throw invalid('err needs a code and a message')
  }

  return withData({ code, message }, map.get(2))
}

function errorInfo(value: CborValue | undefined): ErrorInfo | undefined {
  if (value !== undefined && !(value instanceof Map)) {
    throw invalid('err must be a map with integer keys')
  }

  return value && parseError(value)
}

function errorMap(err: ErrorInfo): CborMap {
  const map: CborMap = new Map<number, CborValue>([[0, err.code], [1, err.message]])
  if (err.data !== undefined) {
    map.set(2, err.data)
  }

  return map
}

function kindOf(value: CborValue | undefined): MessageKind {
  const kind = MESSAGE_KINDS[uint(value, 'k') ?? -1]
  if (kind === undefined) {
    throw invalid(`unknown kind ${String(value)}`)
  }

  return kind
}

function fieldsOf(map: CborMap, kind: MessageKind): Envelope {
  const key = (field: typeof FIELDS[number]) => FIELDS.indexOf(field)
  const envelope: Envelope = { v: PROTOCOL_VERSION, k: kind }
  const values: Record<string, CborValue | ErrorInfo | undefined> = {
    n: text(map.get(key('n')), 'n'),
    p: map.get(key('p')),
    err: errorInfo(map.get(key('err'))),
  }
  for (const field of UINT_FIELDS) {
    values[field] = uint(map.get(key(field)), field)
  }

  for (const [field, value] of Object.entries(values)) {
    if (value !== undefined) {
      Object.assign(envelope, { [field]: value })
    }
  }

  return envelope
}

function checkName(name: string | undefined) {
  if (name !== undefined && new TextEncoder().encode(name).length > MAX_NAME_BYTES) {
    throw invalid(`names are at most ${MAX_NAME_BYTES} bytes`)
  }
}

function checkRequired(envelope: Envelope) {
  const missing = REQUIRED[envelope.k].find(field => envelope[field] === undefined)
  if (missing !== undefined) {
    throw invalid(`a ${envelope.k} needs ${missing}`)
  }
  checkName(envelope.n)
}

function decodeMap(bytes: Uint8Array): CborMap {
  let value: CborValue
  try {
    value = decode(bytes)
  } catch (error) {
    throw invalid(error instanceof CborError ? error.message : String(error))
  }

  if (!(value instanceof Map)) {
    throw invalid('an envelope is a map with integer keys')
  }

  return value
}

function checkVersion(value: CborValue | undefined) {
  const version = uint(value, 'v')
  if (version === undefined) {
    throw invalid('missing v')
  }
  if (version !== PROTOCOL_VERSION) {
    throw new ProtocolError('UNSUPPORTED_VERSION', `protocol version ${version} (this side speaks ${PROTOCOL_VERSION})`)
  }
}

/**
 * Decodes and validates one envelope.
 *
 * @param bytes the CBOR of one message
 * @returns the envelope
 * @throws {ProtocolError} `INVALID_MESSAGE` or `UNSUPPORTED_VERSION`
 */
export function decodeEnvelope(bytes: Uint8Array): Envelope {
  const map = decodeMap(bytes)
  checkVersion(map.get(0))
  const envelope = fieldsOf(map, kindOf(map.get(1)))
  checkRequired(envelope)

  return envelope
}

/**
 * Encodes an envelope (fields in key order; `v` is always PROTOCOL_VERSION).
 *
 * @param envelope the message
 * @returns its CBOR bytes
 */
export function encodeEnvelope(envelope: Omit<Envelope, 'v'> & { v?: number }): Uint8Array {
  const complete: Envelope = { ...envelope, v: PROTOCOL_VERSION }
  checkRequired(complete)
  const wire: Partial<Record<typeof FIELDS[number], CborValue | undefined>> = {
    ...complete,
    k: MESSAGE_KINDS.indexOf(complete.k),
    err: complete.err && errorMap(complete.err),
  }
  const map: CborMap = new Map()
  FIELDS.forEach((field, key) => {
    const value = wire[field]
    if (value !== undefined) {
      map.set(key, value)
    }
  })

  return encode(map)
}
