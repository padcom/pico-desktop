// `pico-desktop test`: the contracts regenerated, then the frontend's Vitest and the backend's
// Pico tests. Both run even if the first fails; the result is the first failure.
import { readFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import { dirname, join } from 'node:path'
import type { ResolvedConfig } from './config'
import { generate } from './generate'
import type { Output } from './io'
import { passthrough } from './processes'

function vitestBin(frontend: string): string {
  const manifest = createRequire(join(frontend, 'package.json')).resolve('vitest/package.json')
  const { bin } = JSON.parse(readFileSync(manifest, 'utf8')) as { bin: string | Record<string, string> }

  return join(dirname(manifest), typeof bin === 'string' ? bin : bin.vitest ?? 'vitest.mjs')
}

/**
 * Runs every test.
 *
 * @param config the app
 * @param io where output goes
 * @returns 0 when all pass
 */
export async function test(config: ResolvedConfig, io: Output): Promise<number> {
  generate(config)
  const vitest = [vitestBin(config.frontend.root), 'run']
  const frontend = await passthrough(process.execPath, vitest, { cwd: config.frontend.root, io, prefix: 'vitest │ ' })
  const backend = await passthrough('pico', ['test', '.'], { cwd: config.backend.root, io, prefix: 'pico │ ' })

  return frontend === 0 ? backend : frontend
}
