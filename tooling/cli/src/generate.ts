// `pico-desktop generate`: the contracts → the frontend's TypeScript and the backend's Pico. A
// file is only written when it changes, so watchers don't rebuild for nothing.
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { dirname, relative } from 'node:path'
import { generatePico, generateTypeScript, readCddl } from '@pico-desktop/schema-tooling'
import type { ContractConfig, ResolvedConfig } from './config'

/** Where the generated Pico code finds the runtime: communicator-pico, as a package. */
export const PICO_RUNTIME = 'pico-desktop/communicator'

function writeIfChanged(path: string, text: string): boolean {
  if (existsSync(path) && readFileSync(path, 'utf8') === text) {
    return false
  }
  mkdirSync(dirname(path), { recursive: true })
  writeFileSync(path, text)

  return true
}

function generateOne(root: string, c: ContractConfig): string[] {
  const contract = readCddl(c.source)
  const source = relative(root, c.source)
  const outputs: [string, string][] = [
    [c.typescript, generateTypeScript(contract)],
    [c.pico, generatePico(contract, { source, runtime: PICO_RUNTIME, dispatchers: false })],
  ]

  return outputs.filter(([path, text]) => writeIfChanged(path, text)).map(([path]) => relative(root, path))
}

/**
 * Generates every contract's modules.
 *
 * @param config the app
 * @returns the files that changed (relative to the app's root)
 */
export function generate(config: ResolvedConfig): string[] {
  return config.contracts.flatMap(c => generateOne(config.root, c))
}

