import { describe, it, expect } from 'vitest'
import { decode as oracleDecode, encode as oracleEncode } from 'cbor2'
import vectors from '../vectors/cbor.json' with { type: 'json' }
import { type CborValue, CborError, decode, encode } from './cbor'
import { type Notation, fromNotation, hex } from './vectors'

describe('CBOR vectors', () => {
  for (const { name, value, hex: bytes } of vectors.roundtrip) {
    it(`encodes ${name}`, () => {
      expect(hex.encode(encode(fromNotation(value as Notation)))).toBe(bytes)
    })
    it(`decodes ${name}`, () => {
      expect(decode(hex.decode(bytes))).toEqual(fromNotation(value as Notation))
    })
  }

  for (const { name, value, hex: bytes } of vectors.decode) {
    it(`accepts ${name}`, () => {
      expect(decode(hex.decode(bytes))).toEqual(fromNotation(value as Notation))
    })
  }

  for (const { name, hex: bytes } of vectors.invalid) {
    it(`rejects ${name}`, () => {
      expect(() => decode(hex.decode(bytes))).toThrow(CborError)
    })
  }
})

describe('CBOR encoding rules', () => {
  it('rejects values outside the profile', () => {
    expect(() => encode(undefined as unknown as CborValue)).toThrow(CborError)
    expect(() => encode([undefined] as unknown as CborValue)).toThrow(CborError)
    expect(() => encode(2n ** 64n)).toThrow(CborError)
    expect(() => encode(-(2n ** 64n) - 1n)).toThrow(CborError)
    expect(() => encode(new Map([[-1, 0]]))).toThrow(CborError)
    expect(() => encode((() => 1) as unknown as CborValue)).toThrow(CborError)
  })

  it('skips undefined object properties', () => {
    expect(hex.encode(encode({ a: 1, b: undefined }))).toBe('a1616101')
  })

  it('limits nesting to 32 levels when encoding', () => {
    let deep: CborValue = 0
    for (let i = 0; i < 33; i++) {
      deep = [deep]
    }
    expect(() => encode(deep)).toThrow(CborError)
  })
})

describe('CBOR decoding details', () => {
  it('treats a __proto__ key as data', () => {
    const decoded = decode(hex.decode('a1695f5f70726f746f5f5f01')) as Record<string, unknown>
    expect(Object.getPrototypeOf(decoded)).toBe(Object.prototype)
    expect(Object.keys(decoded)).toEqual(['__proto__'])
  })

  it('decodes a byte string as an independent copy', () => {
    const input = hex.decode('43010203')
    const out = decode(input) as Uint8Array
    input[1] = 9
    expect([...out]).toEqual([1, 2, 3])
  })
})

/**
 * A deterministic pseudo-random number source (property tests must be reproducible).
 *
 * @param seed where the sequence starts
 * @returns a function giving the next number in [0, 1)
 */
function random(seed: number) {
  let state = seed

  return () => {
    state = (state * 1103515245 + 12345) % 2 ** 31

    return state / 2 ** 31
  }
}

/**
 * Random scalar values of every kind the profile has.
 *
 * @param next the random source
 * @returns a function giving the next leaf value
 */
function leaves(next: () => number) {
  const int = (max: number) => Math.floor(next() * max)
  const choices: (() => CborValue)[] = [
    () => int(2 ** 32) - 2 ** 31,
    () => BigInt(int(2 ** 30)) * 2n ** 34n,
    () => next() * 1e6 - 5e5,
    () => `text-${int(1000)}ü水`,
    () => Uint8Array.from({ length: int(8) }, () => int(256)),
    () => next() < 0.5,
    () => null,
  ]

  return () => choices[int(choices.length)]!()
}

/**
 * Random nested values (arrays and text-keyed maps up to 5 levels deep).
 *
 * @param seed where the sequence starts
 * @returns a function giving the next value
 */
function generator(seed: number) {
  const next = random(seed)
  const leaf = leaves(next)
  const value = (depth: number): CborValue => {
    if (depth > 4 || next() < 0.4) {
      return leaf()
    }
    const length = Math.floor(next() * 5)

    return next() < 0.5
      ? Array.from({ length }, () => value(depth + 1))
      : Object.fromEntries(Array.from({ length }, (_, i) => [`k${i}`, value(depth + 1)]))
  }

  return () => value(0)
}

describe('CBOR against an independent implementation (cbor2)', () => {
  const next = generator(42)
  const samples = Array.from({ length: 300 }, next)

  it('cbor2 decodes what we encode', () => {
    for (const sample of samples) {
      expect(oracleDecode(encode(sample))).toEqual(sample)
    }
  })

  it('we decode what cbor2 encodes', () => {
    for (const sample of samples) {
      expect(decode(oracleEncode(sample))).toEqual(sample)
    }
  })
})
