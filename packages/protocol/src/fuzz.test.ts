import { describe, expect, it } from 'vitest'
import { CborError, decode, encode } from './cbor'
import { ProtocolError, decodeEnvelope, encodeEnvelope } from './envelope'

// Untrusted bytes (what the bridge and the backend deliver) through the decoders: random bytes and
// mutations of valid envelopes. They may only throw CborError or ProtocolError, never anything
// else, and what they accept must survive a round trip. Deterministic (a fixed seed).

function rng(seed: number) {
  let s = seed >>> 0

  return () => {
    s = (s + 0x6d2b79f5) >>> 0
    let t = Math.imul(s ^ (s >>> 15), 1 | s)
    t ^= t + Math.imul(t ^ (t >>> 7), 61 | t)

    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}

const seeds = [
  encodeEnvelope({ k: 'lifecycle', n: 'hello', p: { protocol: [1, 1], role: 'frontend' } }),
  encodeEnvelope({
    k: 'request', id: 42, n: 'greeting.hello', p: { a: [1, -1, 'x'], b: new Uint8Array([0, 255]), c: true, d: 1.5 },
  }),
  encodeEnvelope({ k: 'error', id: 9, err: { code: 'BUSY', message: 'later' } }),
  encodeEnvelope({ k: 'event', n: 'tick', s: 5, d: 2, p: null }),
  encodeEnvelope({ k: 'cancel', id: 1 }),
]

const EDGES = [0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1f, 0x5f, 0x7f, 0x9f, 0xbf, 0xc0, 0xd8, 0xf7, 0xf8, 0xf9, 0xff, 0x00]

function mutate(valid: Uint8Array, random: () => number): Uint8Array {
  const bytes = [...valid]
  const at = () => Math.floor(random() * Math.max(bytes.length, 1))
  const ops = [
    () => { bytes[at()] = Math.floor(random() * 256) },
    () => { bytes.splice(at(), 0, Math.floor(random() * 256)) },
    () => { bytes.splice(at(), 1) },
    () => { bytes.length = at() },
    () => { bytes[at()] = EDGES[Math.floor(random() * EDGES.length)]! },
  ]
  for (let i = 0; i < 1 + Math.floor(random() * 4); i++) {
    ops[Math.floor(random() * ops.length)]!()
  }

  return new Uint8Array(bytes)
}

function input(i: number, random: () => number): Uint8Array {
  if (i % 4 === 0) {
    return new Uint8Array(Math.floor(random() * 48)).map(() => Math.floor(random() * 256))
  }

  return mutate(seeds[Math.floor(random() * seeds.length)]!, random)
}

const decodes = (f: () => unknown) => {
  try {
    f()

    return true
  } catch (error) {
    if (!(error instanceof CborError) && !(error instanceof ProtocolError)) {
      throw error
    }

    return false
  }
}

describe('decoding untrusted bytes', () => {
  it('throws only CborError or ProtocolError, and what it accepts round-trips', () => {
    const random = rng(0x5eed)
    let accepted = 0
    for (let i = 0; i < 50_000; i++) {
      const bytes = input(i, random)
      if (decodes(() => decode(bytes))) {
        expect(decode(encode(decode(bytes)))).toEqual(decode(bytes))
      }
      if (decodes(() => decodeEnvelope(bytes))) {
        accepted++
        expect(decodeEnvelope(encodeEnvelope(decodeEnvelope(bytes)))).toEqual(decodeEnvelope(bytes))
      }
    }
    expect(accepted).toBeGreaterThan(100)
  })
})
