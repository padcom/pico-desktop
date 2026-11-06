// CDDL (RFC 8610) → contract model, through the `cddl` package's parser. Covers the subset the
// protocol needs: maps with optional members, arrays, primitives, `.size`/`.le` on uints, choices
// of text literals (enums), choices of maps with a common literal member (unions), references.
import { parse } from 'cddl'
import type { Contract, Field, Service, Type } from './ir'
import { resolveUnion } from './unions'

// The parser's AST is loosely typed (and not consistently: a member's `Type` is sometimes an
// array of choices, sometimes a single entry).
type Node = Record<string, unknown> | string
type Entries = Node | Node[]
type Rule = Record<string, unknown>

const PRIMITIVES: Record<string, Type> = {
  tstr: { kind: 'text' },
  text: { kind: 'text' },
  bstr: { kind: 'bytes' },
  bytes: { kind: 'bytes' },
  uint: { kind: 'uint' },
  int: { kind: 'int' },
  float: { kind: 'float' },
  bool: { kind: 'bool' },
  null: { kind: 'null' },
  nil: { kind: 'null' },
}

const asList = (entries: Entries): Node[] => (Array.isArray(entries) ? entries : [entries])

function primitive(name: string): Type {
  const type = PRIMITIVES[name]
  if (!type) {
    throw new Error(`unsupported CDDL type ${name}`)
  }

  return type
}

// `.size n` in bytes: full 64-bit integers are plain `uint` in the model.
const sized = (bytes: number): Type => (bytes >= 8 ? { kind: 'uint' } : { kind: 'uint', bits: (bytes * 8) as 8 | 16 | 32 })

const CONTROLS: Record<string, (value: number) => Type> = {
  size: sized,
  le: max => ({ kind: 'uint', max }),
}

function control(operator: { Type: string, Value: { Value: number } }): Type {
  const make = CONTROLS[operator.Type]
  if (!make) {
    throw new Error(`unsupported control .${operator.Type}`)
  }

  return make(operator.Value.Value)
}

function sizedUint(node: Rule): Type {
  const operator = node.Operator as { Type: string, Value: { Value: number } } | undefined

  return operator ? control(operator) : { kind: 'uint' }
}

export class CddlReader {
  readonly types: { name: string, type: Type }[] = []

  private readonly byNodeType: Record<string, (node: Rule) => Type> = {
    uint: node => sizedUint(node),
    literal: node => ({ kind: 'enum', values: [String(node.Value)] }),
    array: node => this.array(node),
    group: node => this.group(node),
  }

  constructor(private readonly rules: Rule[]) {}

  /**
   * One CDDL type expression (with its choices) as a model type.
   *
   * @param entries the parser's type entries
   * @returns the model type
   */
  type(entries: Entries): Type {
    const list = asList(entries)

    return list.length > 1 ? this.choice(list) : this.single(list[0]!)
  }

  /**
   * The whole contract: every rule but `services` is a named type; `services` lists the methods,
   * events and subscriptions.
   *
   * @returns the contract
   */
  contract(): Contract {
    for (const rule of this.rules.filter(r => r.Name !== 'services')) {
      this.types.push({ name: String(rule.Name), type: this.rule(rule) })
    }
    const registry = this.rules.find(r => r.Name === 'services')
    const services = ((registry?.Properties ?? []) as Rule[]).map(p => this.service(p))

    return { types: this.types, services }
  }

  private single(node: Node): Type {
    if (typeof node === 'string') {
      return primitive(node)
    }
    const read = this.byNodeType[String(node.Type)]

    return read ? read(node) : primitive(String(node.Type))
  }

  private group(node: Rule): Type {
    if (typeof node.Value === 'string') {
      return { kind: 'ref', name: node.Value }
    }

    return { kind: 'struct', fields: (node.Properties as Rule[]).map(p => this.field(p)) }
  }

  private array(node: Rule): Type {
    const [only] = node.Values as Rule[]

    return { kind: 'array', items: this.type(only!.Type as Entries) }
  }

  private field(property: Rule): Field {
    const occurrence = property.Occurrence as { n: number }

    return { name: String(property.Name), type: this.type(property.Type as Entries), optional: occurrence.n === 0 }
  }

  private choice(list: Node[]): Type {
    if (list.every(n => typeof n !== 'string' && n.Type === 'literal')) {
      return { kind: 'enum', values: list.map(n => String((n as Rule).Value)) }
    }

    return resolveUnion(list.map(n => this.single(n)), name => this.named(name))
  }

  /**
   * A rule's type by name (for unions, whose alternatives may be declared later).
   *
   * @param name the rule
   * @returns its type, if there's such a rule
   */
  private named(name: string): Type | undefined {
    const rule = this.rules.find(r => r.Name === name)

    return rule && this.rule(rule)
  }

  private rule(rule: Rule): Type {
    return rule.Type === 'variable' ? this.type(rule.PropertyType as Entries) : this.group(rule)
  }

  private service(property: Rule): Service {
    const [first, ...rest] = (asList(property.Type as Entries)[0] as Rule).Values as Rule[]
    // The parser reads the quoted tag ("method") as a reference to a rule of that name.
    const tag = (asList(first!.Type as Entries)[0] as Rule).Value as Service['kind']
    const [a, b] = rest.map(v => this.type(v.Type as Entries)) as [Type, Type | undefined]
    const name = String(property.Name)
    const build: Record<Service['kind'], () => Service> = {
      method: () => ({ kind: 'method', name, request: a, response: b! }),
      event: () => ({ kind: 'event', name, payload: a }),
      subscription: () => ({ kind: 'subscription', name, params: a, value: b! }),
    }

    return build[tag]()
  }
}

/**
 * Reads a CDDL contract file.
 *
 * @param path the .cddl file
 * @returns the contract model
 */
export function readCddl(path: string): Contract {
  return new CddlReader(parse(path) as unknown as Rule[]).contract()
}
