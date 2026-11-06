import { resolve } from 'node:path'
import { describe, expect, it } from 'vitest'
import { readCddl } from './from-cddl'
import { generatePico } from './generate-pico'

const here = (path: string) => resolve(import.meta.dirname, path)

describe('the host services contract', () => {
  it('generates communicator-pico/generated/host.pi', async () => {
    const contract = readCddl(here('../../communicator-pico/contracts/host.cddl'))
    const pico = generatePico(contract, { source: 'the host services contract (contracts/host.cddl)', dispatchers: false })
    await expect(pico).toMatchFileSnapshot('../../communicator-pico/generated/host.pi')
  })
})
