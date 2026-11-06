// Building the Pico backend. A build goes to a temporary file that replaces the backend only once
// it succeeded, so a broken edit leaves the running backend (and the next restart) alone.
import { mkdirSync, renameSync } from 'node:fs'
import { dirname, join } from 'node:path'
import type { ResolvedConfig } from './config'
import type { Output } from './io'
import { capture } from './processes'

/**
 * Where `dev` keeps its build of the backend.
 *
 * @param config the app
 * @returns the executable's path
 */
export function devBackend(config: ResolvedConfig): string {
  return join(config.root, '.pico-desktop', 'dev', 'backend')
}

/**
 * Builds the backend to `out`.
 *
 * @param config the app
 * @param out the executable to produce
 * @param io where the compiler's errors go
 * @returns whether it built
 */
export async function buildBackend(config: ResolvedConfig, out: string, io: Output): Promise<boolean> {
  mkdirSync(dirname(out), { recursive: true })
  const next = `${out}.next`
  // An absolute entry path: pico-lang #93 (fixed by PR #94).
  const entry = join(config.backend.root, config.backend.entry)
  const { code, output } = await capture('pico', ['build', entry, '-o', next], config.backend.root)
  if (code !== 0) {
    output.trimEnd().split('\n').forEach(line => io.err(`pico │ ${line}`))

    return false
  }
  renameSync(next, out)

  return true
}
