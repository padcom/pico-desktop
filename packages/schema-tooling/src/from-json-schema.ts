// JSON Schema (2020-12) + service manifest → contract model. Covers the same subset as the CDDL
// front-end; byte strings and 64-bit integers need the `x-cbor` extension.
import { readFileSync } from 'node:fs'
import { bitsFor, type Contract, type Field, type Service, type Type } from './ir'
import { resolveUnion } from './unions'

interface Schema {
  'type'?: string
  'x-cbor'?: 'bytes' | 'uint64'
  'minimum'?: number
  'maximum'?: number
  'enum'?: string[]
  'const'?: string
  'items'?: Schema
  'properties'?: Record<string, Schema>
  'required'?: string[]
  'oneOf'?: Schema[]
  '$ref'?: string
}

type Manifest = Record<string, Record<string, Schema> & { kind: Service['kind'] }>

const FULL_WIDTHS = new Set([0xff, 0xffff, 0xffffffff])

function bounded(max: number | undefined): Type {
  if (max === undefined) {
    return { kind: 'uint' }
  }

  return FULL_WIDTHS.has(max) ? { kind: 'uint', bits: bitsFor(max) } : { kind: 'uint', max }
}

function integer(schema: Schema): Type {
  if ((schema.minimum ?? -1) < 0) {
    return { kind: 'int' }
  }

  return schema['x-cbor'] === 'uint64' ? { kind: 'uint' } : bounded(schema.maximum)
}

const SIMPLE: Record<string, Type> = {
  string: { kind: 'text' },
  number: { kind: 'float' },
  boolean: { kind: 'bool' },
  null: { kind: 'null' },
}

export class JsonSchemaReader {
  readonly types: { name: string, type: Type }[] = []
  private defs: Record<string, Schema> = {}

  private readonly byType: Record<string, (schema: Schema) => Type> = {
    integer,
    array: schema => ({ kind: 'array', items: this.type(schema.items!) }),
    object: schema => ({ kind: 'struct', fields: this.fields(schema) }),
    ...Object.fromEntries(Object.entries(SIMPLE).map(([name, type]) => [name, () => type])),
  }

  /**
   * One schema as a model type.
   *
   * @param schema the schema
   * @returns the model type
   */
  type(schema: Schema): Type {
    if (schema.$ref) {
      return { kind: 'ref', name: schema.$ref.replace('#/$defs/', '') }
    }

    return schema['x-cbor'] === 'bytes' ? { kind: 'bytes' } : this.structural(schema) ?? this.simple(schema)
  }

  /**
   * The contract from the schema's `$defs` and the service manifest.
   *
   * @param defs the `$defs` of the schema file
   * @param manifest the services
   * @returns the contract
   */
  contract(defs: Record<string, Schema>, manifest: Manifest): Contract {
    this.defs = defs

    for (const [name, schema] of Object.entries(defs)) {
      this.types.push({ name, type: this.type(schema) })
    }
    const services = Object.entries(manifest).filter(([name]) => !name.startsWith('$')).map(([name, s]) => this.service(name, s))

    return { types: this.types, services }
  }

  private service(name: string, s: Manifest[string]): Service {
    const build: Record<Service['kind'], () => Service> = {
      method: () => ({ kind: 'method', name, request: this.type(s.request!), response: this.type(s.response!) }),
      event: () => ({ kind: 'event', name, payload: this.type(s.payload!) }),
      subscription: () => ({ kind: 'subscription', name, params: this.type(s.params!), value: this.type(s.value!) }),
    }

    return build[s.kind]()
  }

  private structural(schema: Schema): Type | undefined {
    if (schema.enum) {
      return { kind: 'enum', values: schema.enum }
    }
    if (schema.const !== undefined) {
      return { kind: 'enum', values: [schema.const] }
    }

    return schema.oneOf && resolveUnion(schema.oneOf.map(s => this.type(s)), name => this.named(name))
  }

  private named(name: string): Type | undefined {
    const schema = this.defs[name]

    return schema && this.type(schema)
  }

  private simple(schema: Schema): Type {
    const read = this.byType[schema.type ?? '']
    if (!read) {
      throw new Error(`unsupported schema ${JSON.stringify(schema)}`)
    }

    return read(schema)
  }

  private fields(schema: Schema): Field[] {
    const required = new Set(schema.required ?? [])

    return Object.entries(schema.properties ?? {}).map(([name, s]) => ({
      name,
      type: this.type(s),
      optional: !required.has(name),
    }))
  }
}

/**
 * Reads a JSON Schema contract and its service manifest.
 *
 * @param schemaPath the .schema.json file (types in `$defs`)
 * @param servicesPath the service manifest
 * @returns the contract model
 */
export function readJsonSchema(schemaPath: string, servicesPath: string): Contract {
  const schema = JSON.parse(readFileSync(schemaPath, 'utf8')) as { $defs: Record<string, Schema> }
  const manifest = JSON.parse(readFileSync(servicesPath, 'utf8')) as Manifest

  return new JsonSchemaReader().contract(schema.$defs, manifest)
}
