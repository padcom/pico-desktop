// Generates the frontend's types and validators and the backend's types, decoders and encoders
// from contracts/hello.cddl (ADR-0005). Run after changing the contract: pnpm generate.
import { writeFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { generatePico, generateTypeScript, readCddl } from '@pico-desktop/schema-tooling'

const here = path => resolve(import.meta.dirname, '..', path)
const contract = readCddl(here('contracts/hello.cddl'))

writeFileSync(here('frontend/src/generated/hello.ts'), generateTypeScript(contract))
writeFileSync(
  here('backend/generated/hello.pi'),
  generatePico(contract, { source: 'contracts/hello.cddl', runtime: 'pico-desktop/communicator', dispatchers: false }),
)
