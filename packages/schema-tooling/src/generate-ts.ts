// Contract model → TypeScript: types, runtime validators (closed maps, integer ranges, byte
// strings) and the service registry the Communicator is typed by.
import type { Contract, Field, Service, Type } from './ir'

/**
 * A contract name as a TypeScript/Pico type name: `serial-port` → `SerialPort`.
 *
 * @param name the contract name
 * @returns the type name
 */
export const typeName = (name: string) => name.split(/[-_.]/).map(part => part.charAt(0).toUpperCase() + part.slice(1)).join('')

const UINT_TS: Record<number, string> = { 8: 'number', 16: 'number', 32: 'number', 64: 'number | bigint' }

const SCALAR_TS: Record<string, string> = {
  int: 'number | bigint',
  float: 'number',
  text: 'string',
  bytes: 'Uint8Array',
  bool: 'boolean',
  null: 'null',
}

const UINT_MAX: Record<number, string> = { 8: '0xff', 16: '0xffff', 32: '0xffffffff' }

const ANY_UINT = 'uint(value, path, Number.MAX_SAFE_INTEGER) && (typeof value !== \'bigint\' || value < 2n ** 64n)'

const SCALAR_CHECKS: Record<string, string> = {
  int: '(Number.isSafeInteger(value) || typeof value === \'bigint\') || fail(path, \'an integer\')',
  float: 'typeof value === \'number\' || fail(path, \'a number\')',
  text: 'typeof value === \'string\' || fail(path, \'text\')',
  bytes: 'value instanceof Uint8Array || fail(path, \'bytes\')',
  bool: 'typeof value === \'boolean\' || fail(path, \'true or false\')',
  null: 'value === null || fail(path, \'null\')',
}

type Of<K extends Type['kind']> = Extract<Type, { kind: K }>

/** TypeScript spellings of model types and the validator expressions for them. */
export class TypeScriptWriter {
  /**
   * The TypeScript type for a model type.
   *
   * @param type the model type
   * @returns its TypeScript spelling
   */
  type(type: Type): string {
    const scalar = SCALAR_TS[type.kind]
    if (scalar) {
      return scalar
    }
    const compound: Partial<Record<Type['kind'], () => string>> = {
      uint: () => UINT_TS[(type as Of<'uint'>).bits ?? 64]!,
      array: () => `(${this.type((type as Of<'array'>).items)})[]`,
      struct: () => `{ ${(type as Of<'struct'>).fields.map(f => this.field(f)).join(', ')} }`,
      enum: () => (type as Of<'enum'>).values.map(v => JSON.stringify(v)).join(' | '),
      union: () => (type as Of<'union'>).variants.map(v => this.type(v.type)).join(' | '),
      ref: () => typeName((type as Of<'ref'>).name),
    }

    return compound[type.kind]!()
  }

  /**
   * A boolean expression that's true when `value` (at `path`) has the type, and otherwise
   * records the first problem through `fail()`.
   *
   * @param type the model type
   * @returns the expression
   */
  check(type: Type): string {
    const scalar = SCALAR_CHECKS[type.kind]
    if (scalar) {
      return scalar
    }
    const compound: Partial<Record<Type['kind'], () => string>> = {
      uint: () => this.uint(type as Of<'uint'>),
      array: () => `list(value, path, (value, path) => ${this.check((type as Of<'array'>).items)})`,
      struct: () => this.struct((type as Of<'struct'>).fields),
      enum: () => `oneOf(value, path, ${JSON.stringify((type as Of<'enum'>).values)})`,
      union: () => this.union(type as Of<'union'>),
      ref: () => `validate${typeName((type as Of<'ref'>).name)}Into(value, path)`,
    }

    return compound[type.kind]!()
  }

  service(s: Service): string {
    const name = JSON.stringify(s.name)
    const shapes: Record<Service['kind'], () => string> = {
      method: () => {
        const m = s as Extract<Service, { kind: 'method' }>

        return `${name}: { kind: 'method', request: ${this.type(m.request)}, response: ${this.type(m.response)} }`
      },
      event: () => `${name}: { kind: 'event', payload: ${this.type((s as Extract<Service, { kind: 'event' }>).payload)} }`,
      subscription: () => {
        const sub = s as Extract<Service, { kind: 'subscription' }>

        return `${name}: { kind: 'subscription', params: ${this.type(sub.params)}, value: ${this.type(sub.value)} }`
      },
    }

    return `  ${shapes[s.kind]()}`
  }

  private field(field: Field): string {
    return `${JSON.stringify(field.name)}${field.optional ? '?' : ''}: ${this.type(field.type)}`
  }

  private uint(type: Of<'uint'>): string {
    const max = type.max !== undefined ? String(type.max) : UINT_MAX[type.bits ?? 64]

    return max ? `uint(value, path, ${max})` : ANY_UINT
  }

  private fieldCheck(f: Field): string {
    const at = `value[${JSON.stringify(f.name)}]`
    const where = `\`\${path}.${f.name}\``
    const sub = `nested(${at}, ${where}, (value, path) => ${this.check(f.type)})`

    return f.optional ? `(${at} === undefined || ${sub})` : `(${at} === undefined ? fail(${where}, 'present') : ${sub})`
  }

  private struct(fields: Field[]): string {
    const known = JSON.stringify(fields.map(f => f.name))

    return [`map(value, path, ${known})`, ...fields.map(f => this.fieldCheck(f))].join(' && ')
  }

  private union(u: Of<'union'>): string {
    const cases = u.variants.map(v => `${JSON.stringify(v.value)}: (value, path) => ${this.check(v.type)}`).join(', ')

    return `tagged(value, path, ${JSON.stringify(u.tag)}, { ${cases} })`
  }
}

const RUNTIME = `// Generated by @pico-desktop/schema-tooling: do not edit.
type Check = (value: any, path: string) => boolean

let problem: string | null = null

const fail = (path: string, expected: string) => {
  problem ??= \`\${path}: expected \${expected}\`

  return false
}

const isMap = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value)
    && !(value instanceof Uint8Array) && !(value instanceof Map)

const map = (value: unknown, path: string, known: string[]) => {
  if (!isMap(value)) {
    return fail(path, 'a map')
  }
  const extra = Object.keys(value).find(key => !known.includes(key))

  return extra === undefined || fail(\`\${path}.\${extra}\`, 'no such member')
}

const nested = (value: unknown, path: string, test: Check) => test(value, path)

const uint = (value: unknown, path: string, max: number) =>
  ((typeof value === 'number' && Number.isSafeInteger(value) && value >= 0 && value <= max)
    || (typeof value === 'bigint' && value >= 0n)) || fail(path, \`an unsigned integer up to \${max}\`)

const list = (value: unknown, path: string, test: Check) => {
  if (!Array.isArray(value)) {
    return fail(path, 'an array')
  }

  return value.every((item: unknown, i: number) => test(item, \`\${path}[\${i}]\`))
}

const oneOf = (value: unknown, path: string, values: string[]) =>
  (typeof value === 'string' && values.includes(value)) || fail(path, values.map(v => JSON.stringify(v)).join(' or '))

const tagged = (value: unknown, path: string, tag: string, variants: Record<string, Check>) => {
  const variant = isMap(value) && typeof value[tag] === 'string' ? variants[value[tag]] : undefined

  const expected = Object.keys(variants).map(v => JSON.stringify(v)).join(' or ')

  return variant ? variant(value, path) : fail(\`\${path}.\${tag}\`, expected)
}

const run = (test: Check, value: unknown): string | null => {
  problem = null

  return test(value, '$') ? null : problem ?? '$: invalid'
}
`

/**
 * The TypeScript module for a contract.
 *
 * @param contract the contract model
 * @returns the module's source
 */
export function generateTypeScript(contract: Contract): string {
  const w = new TypeScriptWriter()
  const parts = [RUNTIME]
  for (const { name, type } of contract.types) {
    const ts = typeName(name)
    parts.push(`export type ${ts} = ${w.type(type)}`)
    parts.push(`function validate${ts}Into(value: any, path: string): boolean {\n  return ${w.check(type)}\n}`)
    const doc = `/** The first problem with \`value\` as a ${name}, or null. */`
    parts.push(`${doc}\nexport const validate${ts} = (value: unknown) => run(validate${ts}Into, value)`)
  }
  const services = contract.services.map(s => w.service(s)).join('\n')
  parts.push(`/** The services, for a typed Communicator. */\nexport interface Services {\n${services}\n}`)

  return `${parts.join('\n\n')}\n`
}
