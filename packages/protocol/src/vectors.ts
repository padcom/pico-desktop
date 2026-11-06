// Reading the shared test vectors (vectors/*.json) into TypeScript values. Used by the tests; the
// Pico codec reads the same files.
import type { CborMap, CborObject, CborValue } from './cbor'

export type Notation = number | string | boolean | null | Notation[] | { [key: string]: Notation }

const fromHex = (text: string) => Uint8Array.from(text.match(/../g) ?? [], byte => parseInt(byte, 16))

export const hex = {
  decode: fromHex,
  encode: (bytes: Uint8Array) => [...bytes].map(b => b.toString(16).padStart(2, '0')).join(''),
}

const specialFloats: Record<string, number> = { NaN, Infinity, '-Infinity': -Infinity, '-0': -0 }

type Reader = (inner: Notation, read: (n: Notation) => CborValue) => CborValue

const wrappers: Record<string, Reader> = {
  $float: inner => (typeof inner === 'string' ? specialFloats[inner]! : inner as number),
  $int: inner => BigInt(inner as string),
  $bytes: inner => fromHex(inner as string),
  $map: (inner, read) => new Map((inner as [number, Notation][]).map(([k, v]) => [k, read(v)])) as CborMap,
}

function fromObject(object: { [key: string]: Notation }, read: (n: Notation) => CborValue): CborValue {
  const keys = Object.keys(object)
  const wrapper = keys.length === 1 ? wrappers[keys[0]!] : undefined
  if (wrapper) {
    return wrapper(object[keys[0]!]!, read)
  }
  const out: CborObject = {}
  for (const [key, value] of Object.entries(object)) {
    out[key] = read(value)
  }

  return out
}

/**
 * The value a vector describes.
 *
 * @param n the vector's JSON notation
 * @returns the TypeScript value
 */
export function fromNotation(n: Notation): CborValue {
  if (Array.isArray(n)) {
    return n.map(fromNotation)
  }

  return n !== null && typeof n === 'object' ? fromObject(n, fromNotation) : n
}
