import { describe, it, expect } from 'vitest'
import { fileURLToPath } from 'node:url'
import { readCddl } from './from-cddl'
import { readJsonSchema } from './from-json-schema'

const contract = (file: string) => fileURLToPath(new URL(`../contracts/${file}`, import.meta.url))

const fromCddl = readCddl(contract('experiment.cddl'))
const fromJsonSchema = readJsonSchema(contract('experiment.schema.json'), contract('experiment.services.json'))

describe('schema front-ends (ADR-0005 experiment)', () => {
  it('read the same contract from CDDL and from JSON Schema + manifest', () => {
    expect(fromJsonSchema).toEqual(fromCddl)
  })

  it('find every type and service', () => {
    expect(fromCddl.types.map(t => t.name)).toEqual([
      'serial-port', 'open-params', 'write-params', 'packet', 'raw-packet', 'text-packet', 'telemetry', 'tree', 'port-busy',
    ])
    expect(fromCddl.services.map(s => `${s.kind} ${s.name}`)).toEqual([
      'method serial.list',
      'method serial.open',
      'method serial.write',
      'method tree.get',
      'event ui.ready',
      'subscription drone.telemetry',
    ])
  })
})

describe('the contract model', () => {
  it('has the tagged union and sized integers', () => {
    const named = (name: string) => fromCddl.types.find(t => t.name === name)?.type
    expect(named('packet')).toEqual({
      kind: 'union',
      tag: 'kind',
      variants: [
        { value: 'raw', type: { kind: 'ref', name: 'raw-packet' } },
        { value: 'text', type: { kind: 'ref', name: 'text-packet' } },
      ],
    })
    const vendorId = { name: 'vendorId', type: { kind: 'uint', bits: 16 }, optional: true }
    const port = named('serial-port')
    expect(port?.kind === 'struct' ? port.fields[1] : undefined).toEqual(vendorId)
  })
})
