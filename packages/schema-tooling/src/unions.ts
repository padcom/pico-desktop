// Tagged unions, recognized the same way for every front-end: a choice of maps that all have one
// member with a single text value (the tag), different in each.
import type { Type } from './ir'

/** Finds a named type's definition (it may be declared after the union). */
export type Lookup = (name: string) => Type | undefined

function structOf(type: Type, named: Lookup): Type | undefined {
  return type.kind === 'ref' ? named(type.name) : type
}

function singleValue(type: Type): string | undefined {
  return type.kind === 'enum' && type.values.length === 1 ? type.values[0] : undefined
}

function tagValue(type: Type | undefined, tag: string): string | undefined {
  const field = type?.kind === 'struct' ? type.fields.find(f => f.name === tag && !f.optional) : undefined

  return field && singleValue(field.type)
}

function distinctValues(values: (string | undefined)[]): values is string[] {
  return values.every(v => v !== undefined) && new Set(values).size === values.length
}

function findTag(variants: Type[], named: Lookup): { tag: string, values: string[] } | undefined {
  const structs = variants.map(v => structOf(v, named))
  const [first] = structs
  const names = first?.kind === 'struct' ? first.fields.map(f => f.name) : []
  const tag = names.find(name => distinctValues(structs.map(s => tagValue(s, name))))

  return tag === undefined ? undefined : { tag, values: structs.map(s => tagValue(s, tag)!) }
}

/**
 * A choice of types as a tagged union.
 *
 * @param variants the alternatives (structs, or references to named structs)
 * @param named finds named types
 * @returns the union
 * @throws {Error} when the alternatives have no common single-value member
 */
export function resolveUnion(variants: Type[], named: Lookup): Type {
  const found = findTag(variants, named)
  if (!found) {
    throw new Error('a choice of maps needs a member with a different single text value in each (a tag)')
  }

  return { kind: 'union', tag: found.tag, variants: variants.map((type, i) => ({ value: found.values[i]!, type })) }
}
