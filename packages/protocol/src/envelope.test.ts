import { describe, it, expect } from 'vitest'
import vectors from '../vectors/envelope.json' with { type: 'json' }
import { type CborValue, encode } from './cbor'
import {
  type Envelope,
  ERROR_CODES,
  MESSAGE_KINDS,
  PROTOCOL_VERSION,
  ProtocolError,
  decodeEnvelope,
  encodeEnvelope,
} from './envelope'
import { DEFAULT_MAX_MESSAGE_BYTES, FrameDecoder, encodeFrame } from './framing'
import { type Notation, fromNotation, hex } from './vectors'

const asEnvelope = (n: Notation) => {
  const e = fromNotation(n) as unknown as Envelope

  return e
}

describe('envelope vectors', () => {
  for (const { name, envelope, hex: bytes } of vectors.valid) {
    it(`encodes ${name}`, () => {
      expect(hex.encode(encodeEnvelope(asEnvelope(envelope as Notation)))).toBe(bytes)
    })
    it(`decodes ${name}`, () => {
      expect(decodeEnvelope(hex.decode(bytes))).toEqual(asEnvelope(envelope as Notation))
    })
  }

  for (const { name, hex: bytes, code } of vectors.invalid) {
    it(`rejects ${name} with ${code}`, () => {
      expect(() => decodeEnvelope(hex.decode(bytes))).toThrow(expect.objectContaining({ code }))
    })
  }
})

describe('envelope rules', () => {
  it('speaks version 1', () => {
    expect(PROTOCOL_VERSION).toBe(1)
  })

  it('lists each kind and error code once', () => {
    expect(new Set(MESSAGE_KINDS).size).toBe(MESSAGE_KINDS.length)
    expect(new Set(ERROR_CODES).size).toBe(ERROR_CODES.length)
  })

  it('refuses to encode an envelope that misses a required field', () => {
    expect(() => encodeEnvelope({ k: 'request', n: 'x' })).toThrow(ProtocolError)
    expect(() => encodeEnvelope({ k: 'error', id: 1 })).toThrow(ProtocolError)
  })

  it('ignores keys it doesn\'t know', () => {
    const bytes = encode(new Map<number, CborValue>([[0, 1], [1, 3], [3, 'tick'], [42, 'future']]))
    expect(decodeEnvelope(bytes)).toEqual({ v: 1, k: 'event', n: 'tick' })
  })

  it('limits names to 256 bytes', () => {
    expect(() => encodeEnvelope({ k: 'event', n: 'x'.repeat(257) })).toThrow(ProtocolError)
    expect(encodeEnvelope({ k: 'event', n: 'x'.repeat(256) }).length).toBeGreaterThan(256)
  })
})

describe('envelope round trips', () => {
  it('round-trips every kind with a payload', () => {
    const payload = { values: [1, 2n ** 60n, 'ü', new Uint8Array([1, 2])], nested: { ok: true, none: null } }
    const envelopes: Envelope[] = [
      { v: 1, k: 'request', id: 7, n: 'serial.list', p: payload },
      { v: 1, k: 'response', id: 7, p: payload },
      { v: 1, k: 'error', id: 7, err: { code: 'HANDLER_ERROR', message: 'nope', data: { port: 'COM1' } } },
      { v: 1, k: 'event', n: 'drone.telemetry', s: 3, d: 2, p: payload },
      { v: 1, k: 'subscribe', id: 8, n: 'drone.telemetry' },
      { v: 1, k: 'unsubscribe', s: 3 },
      { v: 1, k: 'cancel', id: 7 },
      { v: 1, k: 'lifecycle', n: 'ready', e: 2, w: 1 },
    ]
    for (const envelope of envelopes) {
      expect(decodeEnvelope(encodeEnvelope(envelope))).toEqual(envelope)
    }
  })
})

describe('framing', () => {
  const messages = [
    encodeEnvelope({ k: 'event', n: 'a' }),
    new Uint8Array(70_000).fill(7),
    encodeEnvelope({ k: 'cancel', id: 1 }),
  ]
  const stream = Uint8Array.from(messages.flatMap(m => [...encodeFrame(m)]))

  it('reassembles messages from any chunking', () => {
    for (const size of [1, 3, 4, 5, 1000, stream.length]) {
      const decoder = new FrameDecoder()
      const out: Uint8Array[] = []
      for (let at = 0; at < stream.length; at += size) {
        out.push(...decoder.push(stream.subarray(at, at + size)))
      }
      expect(out).toEqual(messages)
      expect(decoder.pending).toBe(0)
    }
  })

  it('reports a cut-off frame as pending bytes', () => {
    const decoder = new FrameDecoder()
    const firstFrame = 4 + messages[0]!.length
    expect(decoder.push(stream.subarray(0, firstFrame + 3))).toHaveLength(1)
    expect(decoder.pending).toBeGreaterThan(0)
  })
})

describe('framing limits', () => {
  it('rejects zero-length frames and stays failed', () => {
    const decoder = new FrameDecoder()
    expect(() => decoder.push(new Uint8Array([0, 0, 0, 0]))).toThrow(expect.objectContaining({ code: 'INVALID_MESSAGE' }))
    expect(() => decoder.push(encodeFrame(new Uint8Array([1])))).toThrow(ProtocolError)
  })

  it('rejects frames over the limit before buffering them', () => {
    const decoder = new FrameDecoder(1024)
    expect(() => decoder.push(new Uint8Array([0, 0, 4, 1]))).toThrow(expect.objectContaining({ code: 'MESSAGE_TOO_LARGE' }))
    expect(new FrameDecoder().push(encodeFrame(new Uint8Array(DEFAULT_MAX_MESSAGE_BYTES)))).toHaveLength(1)
  })

  it('refuses to frame nothing', () => {
    expect(() => encodeFrame(new Uint8Array(0))).toThrow(ProtocolError)
  })
})
