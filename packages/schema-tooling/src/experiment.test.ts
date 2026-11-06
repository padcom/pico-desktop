// ADR-0005: the experiment contract through both front-ends and both generators. The generated
// files are snapshots (deterministic generation); the corpus checks the TypeScript validators
// here and the Pico decoders in communicator-pico (experiment.test.pi).
import { describe, it, expect } from 'vitest'
import { fileURLToPath } from 'node:url'
import { decode, encode } from '@pico-desktop/protocol'
import { readCddl } from './from-cddl'
import { readJsonSchema } from './from-json-schema'
import { generatePico } from './generate-pico'
import { generateTypeScript, typeName } from './generate-ts'
import { corpus } from './corpus'

const here = (path: string) => fileURLToPath(new URL(path, import.meta.url))
const fromCddl = readCddl(here('../contracts/experiment.cddl'))
const fromJsonSchema = readJsonSchema(here('../contracts/experiment.schema.json'), here('../contracts/experiment.services.json'))
const hex = (bytes: Uint8Array) => [...bytes].map(b => b.toString(16).padStart(2, '0')).join('')

describe('generation', () => {
  it('is the same from either front-end', () => {
    expect(generateTypeScript(fromJsonSchema)).toBe(generateTypeScript(fromCddl))
    expect(generatePico(fromJsonSchema)).toBe(generatePico(fromCddl))
  })

  it('gives the committed TypeScript', async () => {
    await expect(generateTypeScript(fromCddl)).toMatchFileSnapshot('../generated/experiment.ts')
  })

  it('gives the committed Pico', async () => {
    await expect(generatePico(fromCddl)).toMatchFileSnapshot('../../communicator-pico/generated/experiment.pi')
  })

  it('gives the committed corpus', async () => {
    const cases = corpus.map(c => ({ name: c.name, type: c.type, valid: c.valid, hex: hex(encode(c.value)) }))
    const file = '../../communicator-pico/generated/experiment-corpus.json'
    await expect(`${JSON.stringify({ cases }, null, 1)}\n`).toMatchFileSnapshot(file)
  })
})

describe('the generated TypeScript validators', () => {
  for (const c of corpus) {
    it(`${c.valid ? 'accept' : 'reject'} ${c.name}`, async () => {
      const generated = await import('../generated/experiment') as unknown as Record<string, (value: unknown) => string | null>
      const problem = generated[`validate${typeName(c.type)}`]!(decode(encode(c.value)))
      expect(problem === null, problem ?? 'accepted').toBe(c.valid)
    })
  }
})
