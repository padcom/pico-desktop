// The contract model both schema front-ends produce (ADR-0005): what generators for TypeScript
// and Pico need, independent of how the contract was written.

export type Type
  = | { kind: 'uint', bits?: 8 | 16 | 32 | 64, max?: number }
    | { kind: 'int' }
    | { kind: 'float' }
    | { kind: 'text' }
    | { kind: 'bytes' }
    | { kind: 'bool' }
    | { kind: 'null' }
    | { kind: 'array', items: Type }
    | { kind: 'struct', fields: Field[] }
    | { kind: 'enum', values: string[] }
    | { kind: 'union', tag: string, variants: { value: string, type: Type }[] }
    | { kind: 'ref', name: string }

export interface Field {
  name: string
  type: Type
  optional: boolean
}

export type Service
  = | { kind: 'method', name: string, request: Type, response: Type }
    | { kind: 'event', name: string, payload: Type }
    | { kind: 'subscription', name: string, params: Type, value: Type }

export interface Contract {
  /** Named types, in declaration order. */
  types: { name: string, type: Type }[]
  services: Service[]
}

/**
 * The bit size a `uint` with an inclusive maximum needs (for languages with sized integers).
 *
 * @param max the largest value
 * @returns 8, 16, 32 or 64
 */
export function bitsFor(max: number): 8 | 16 | 32 | 64 {
  const sizes = [[0xff, 8], [0xffff, 16], [0xffffffff, 32]] as const
  const found = sizes.find(([limit]) => max <= limit)

  return found ? found[1] : 64
}
