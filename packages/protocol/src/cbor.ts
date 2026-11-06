// CBOR (RFC 8949) restricted to the Pico Desktop profile (docs/protocol.md §3): definite lengths,
// no tags, simple values false/true/null, text-keyed maps in payloads (uint-keyed in envelopes),
// no duplicate keys, bounded nesting. Encoding uses the preferred serialization.

/** A value that can cross the protocol. */
export type CborValue
  = | number
    | bigint
    | string
    | boolean
    | null
    | Uint8Array
    | CborValue[]
    | CborMap
    | CborObject

/** A uint-keyed map (the envelope). */
export type CborMap = Map<number, CborValue>

/** A text-keyed map (payloads); `undefined` properties are skipped when encoding. */
export interface CborObject {
  [key: string]: CborValue | undefined
}

export const MAX_DEPTH = 32

/** A message that breaks the CBOR profile (`INVALID_MESSAGE`). */
export class CborError extends Error {
  readonly code = 'INVALID_MESSAGE'

  constructor(message: string) {
    super(message)
    this.name = 'CborError'
  }
}

const MAX_U64 = (1n << 64n) - 1n
const MAJOR = { uint: 0, negative: 1, bytes: 2, text: 3, array: 4, map: 5, tag: 6, simple: 7 } as const
const textEncoder = new TextEncoder()
const textDecoder = new TextDecoder('utf-8', { fatal: true })

/** Heads after the one-byte form: [below this argument, additional information, argument bytes]. */
const HEAD_FORMS: readonly (readonly [bigint, number, number])[] = [
  [0x100n, 24, 1],
  [0x10000n, 25, 2],
  [0x100000000n, 26, 4],
  [MAX_U64 + 1n, 27, 8],
]

const checkDepth = (depth: number) => {
  if (depth > MAX_DEPTH) {
    throw new CborError(`nesting deeper than ${MAX_DEPTH}`)
  }
}

// ------------------------------------------------------------------------------------- encoding

function uintKey(key: unknown): number {
  if (typeof key !== 'number' || !Number.isSafeInteger(key) || key < 0) {
    throw new CborError(`a Map key must be an unsigned integer, got ${String(key)}`)
  }

  return key
}

class Encoder {
  private buffer = new Uint8Array(256)
  private length = 0

  result(): Uint8Array {
    return this.buffer.slice(0, this.length)
  }

  private readonly byType: Partial<Record<string, (value: never, depth: number) => void>> = {
    number: (value: number) => this.number(value),
    bigint: (value: bigint) => this.integer(value),
    string: (value: string) => this.text(value),
    boolean: (value: boolean) => this.byte(value ? 0xf5 : 0xf4),
    object: (value: object | null, depth: number) => this.object(value, depth),
  }

  item(value: CborValue, depth: number) {
    const encodeAs = this.byType[typeof value]
    if (!encodeAs) {
      throw new CborError(`can't encode a ${typeof value}`)
    }
    encodeAs(value as never, depth)
  }

  private reserve(n: number) {
    if (this.length + n > this.buffer.length) {
      const grown = new Uint8Array(Math.max(this.buffer.length * 2, this.length + n))
      grown.set(this.buffer.subarray(0, this.length))
      this.buffer = grown
    }
  }

  private byte(b: number) {
    this.reserve(1)
    this.buffer[this.length++] = b
  }

  private raw(bytes: Uint8Array) {
    this.reserve(bytes.length)
    this.buffer.set(bytes, this.length)
    this.length += bytes.length
  }

  /**
   * Writes `n` as `size` big-endian bytes.
   *
   * @param n the unsigned value
   * @param size how many bytes
   */
  private uintBE(n: bigint, size: number) {
    for (let shift = BigInt((size - 1) * 8); shift >= 0n; shift -= 8n) {
      this.byte(Number((n >> shift) & 0xffn))
    }
  }

  private head(major: number, n: number | bigint) {
    const argument = BigInt(n)
    if (argument < 24n) {
      this.byte((major << 5) | Number(argument))

      return
    }
    const [, info, size] = HEAD_FORMS.find(([below]) => argument < below)!
    this.byte((major << 5) | info)
    this.uintBE(argument, size)
  }

  private integer(n: bigint) {
    const negative = n < 0n
    const magnitude = negative ? -1n - n : n
    if (magnitude > MAX_U64) {
      throw new CborError(`integer ${n} is outside the CBOR range`)
    }
    this.head(negative ? MAJOR.negative : MAJOR.uint, magnitude)
  }

  private number(n: number) {
    if (Number.isSafeInteger(n) && !Object.is(n, -0)) {
      this.integer(BigInt(n))

      return
    }
    const bytes = new Uint8Array(9)
    bytes[0] = 0xfb
    new DataView(bytes.buffer).setFloat64(1, n)
    this.raw(bytes)
  }

  private text(s: string) {
    const utf8 = textEncoder.encode(s)
    this.head(MAJOR.text, utf8.length)
    this.raw(utf8)
  }

  private object(value: object | null, depth: number) {
    if (value === null) {
      this.byte(0xf6)
    } else if (value instanceof Uint8Array) {
      this.head(MAJOR.bytes, value.length)
      this.raw(value)
    } else {
      checkDepth(depth)
      this.compound(value, depth)
    }
  }

  private compound(value: object, depth: number) {
    if (Array.isArray(value)) {
      this.array(value as unknown[], depth)
    } else if (value instanceof Map) {
      this.entries([...(value as Map<unknown, CborValue>).entries()].map(([k, v]) => [uintKey(k), v]), depth)
    } else {
      this.entries(Object.entries(value as CborObject).filter(([, v]) => v !== undefined) as [string, CborValue][], depth)
    }
  }

  private array(items: unknown[], depth: number) {
    this.head(MAJOR.array, items.length)

    for (const item of items) {
      if (item === undefined) {
        throw new CborError('undefined in an array')
      }
      this.item(item as CborValue, depth + 1)
    }
  }

  private entries(entries: [CborValue, CborValue][], depth: number) {
    this.head(MAJOR.map, entries.length)

    for (const [key, value] of entries) {
      this.item(key, depth + 1)
      this.item(value, depth + 1)
    }
  }
}

/**
 * Encodes `value` with the preferred serialization.
 *
 * @param value the value
 * @returns its CBOR bytes
 */
export function encode(value: CborValue): Uint8Array {
  const encoder = new Encoder()
  encoder.item(value, 1)

  return encoder.result()
}

// ------------------------------------------------------------------------------------- decoding

const ARGUMENT_BYTES: Record<number, number> = { 24: 1, 25: 2, 26: 4, 27: 8 }
const SIMPLE_VALUES: Record<number, boolean | null> = { 20: false, 21: true, 22: null }
const FLOAT_BYTES: Record<number, 2 | 4 | 8> = { 25: 2, 26: 4, 27: 8 }

function halfSpecial(fraction: number, sign: number): number {
  return fraction ? NaN : sign * Infinity
}

function halfToNumber(half: number): number {
  const sign = half & 0x8000 ? -1 : 1
  const exponent = (half >> 10) & 0x1f
  const fraction = half & 0x3ff
  if (exponent === 0x1f) {
    return halfSpecial(fraction, sign)
  }
  const [scale, significand] = exponent === 0 ? [-14, fraction / 1024] : [exponent - 15, 1 + fraction / 1024]

  return sign * 2 ** scale * significand
}

const SAFE = { max: BigInt(Number.MAX_SAFE_INTEGER), min: BigInt(Number.MIN_SAFE_INTEGER) }

const toNumberIfSafe = (n: bigint): number | bigint => (n <= SAFE.max && n >= SAFE.min ? Number(n) : n)

function argumentError(info: number): CborError {
  return new CborError(info === 31
    ? 'indefinite lengths are not allowed'
    : `reserved additional information ${info}`)
}

function simpleError(info: number): CborError {
  const messages: Record<number, string> = { 23: 'undefined is not allowed', 31: 'unexpected break' }

  return new CborError(messages[info] ?? 'simple values other than false, true and null are not allowed')
}

function mapKey(key: CborValue): string | number {
  if (typeof key === 'string' || (typeof key === 'number' && key >= 0)) {
    return key
  }
  throw new CborError('map keys must be text or unsigned integers')
}

function checkKeys(keys: (string | number)[]) {
  if (new Set(keys).size !== keys.length) {
    throw new CborError('duplicate map key')
  }
  const texts = keys.filter(k => typeof k === 'string').length
  if (texts !== 0 && texts !== keys.length) {
    throw new CborError('a map mixes text and integer keys')
  }
}

function buildMap(keys: (string | number)[], values: CborValue[]): CborMap | CborObject {
  checkKeys(keys)

  if (keys.length > 0 && typeof keys[0] === 'number') {
    return new Map(keys.map((k, i) => [k as number, values[i]!]))
  }
  const object: CborObject = {}
  keys.forEach((key, i) => {
    // defineProperty: a `__proto__` key is just a key.
    Object.defineProperty(object, key, { value: values[i], enumerable: true, writable: true, configurable: true })
  })

  return object
}

class Decoder {
  private pos = 0
  private readonly view: DataView
  private readonly majors: ((info: number, depth: number) => CborValue)[] = [
    info => toNumberIfSafe(this.argument(info)),
    info => toNumberIfSafe(-1n - this.argument(info)),
    info => this.byteString(info),
    info => this.text(info),
    (info, depth) => this.array(info, depth),
    (info, depth) => this.map(info, depth),
    () => {
      throw new CborError('tags are not allowed')
    },
    info => this.simple(info),
  ]

  constructor(private readonly bytes: Uint8Array) {
    this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  }

  done(): boolean {
    return this.pos === this.bytes.length
  }

  item(depth: number): CborValue {
    const initial = this.view.getUint8(this.need(1))

    return this.majors[initial >> 5]!(initial & 0x1f, depth)
  }

  /**
   * Checks that `n` more bytes exist.
   *
   * @param n how many bytes are needed
   * @returns the position before them (the reader moves past them)
   */
  private need(n: number): number {
    if (this.pos + n > this.bytes.length) {
      throw new CborError('truncated')
    }
    const at = this.pos
    this.pos += n

    return at
  }

  /**
   * Moves past `n` bytes that `length` already checked.
   *
   * @param n how many bytes
   * @returns the position after them
   */
  private advance(n: number): number {
    this.pos += n

    return this.pos
  }

  private argument(info: number): bigint {
    if (info < 24) {
      return BigInt(info)
    }
    const size = ARGUMENT_BYTES[info]
    if (size === undefined) {
      throw argumentError(info)
    }
    const at = this.need(size)
    let n = 0n
    for (let i = 0; i < size; i++) {
      n = (n << 8n) | BigInt(this.view.getUint8(at + i))
    }

    return n
  }

  /**
   * A length that has to fit in what's left of the message.
   *
   * @param info the head's additional information
   * @param minItemBytes the least bytes one counted item can take
   * @returns the length
   */
  private length(info: number, minItemBytes: number): number {
    const n = this.argument(info)
    if (n * BigInt(minItemBytes) > BigInt(this.bytes.length - this.pos)) {
      throw new CborError('a length beyond the end of the message')
    }

    return Number(n)
  }

  private byteString(info: number): Uint8Array {
    const n = this.length(info, 1)
    const start = this.pos

    return this.bytes.slice(start, this.advance(n))
  }

  private text(info: number): string {
    const n = this.length(info, 1)
    const slice = this.bytes.subarray(this.pos, this.advance(n))
    try {
      return textDecoder.decode(slice)
    } catch {
      throw new CborError('text is not valid UTF-8')
    }
  }

  private array(info: number, depth: number): CborValue[] {
    checkDepth(depth)

    return Array.from({ length: this.length(info, 1) }, () => this.item(depth + 1))
  }

  private map(info: number, depth: number): CborMap | CborObject {
    checkDepth(depth)
    const n = this.length(info, 2)
    const keys: (string | number)[] = []
    const values: CborValue[] = []
    for (let i = 0; i < n; i++) {
      keys.push(mapKey(this.item(depth + 1)))
      values.push(this.item(depth + 1))
    }

    return buildMap(keys, values)
  }

  private simple(info: number): CborValue {
    if (info in SIMPLE_VALUES) {
      return SIMPLE_VALUES[info]!
    }
    const size = FLOAT_BYTES[info]
    if (size === undefined) {
      throw simpleError(info)
    }

    return this.float(size)
  }

  private float(size: 2 | 4 | 8): number {
    const at = this.need(size)
    const readers = {
      2: () => halfToNumber(this.view.getUint16(at)),
      4: () => this.view.getFloat32(at),
      8: () => this.view.getFloat64(at),
    }

    return readers[size]()
  }
}

/**
 * Decodes one complete CBOR item that follows the profile.
 *
 * @param bytes exactly one CBOR data item
 * @returns the value
 * @throws {CborError} when it breaks the profile, is truncated or has trailing bytes
 */
export function decode(bytes: Uint8Array): CborValue {
  const decoder = new Decoder(bytes)
  const value = decoder.item(1)
  if (!decoder.done()) {
    throw new CborError('trailing bytes after the item')
  }

  return value
}
