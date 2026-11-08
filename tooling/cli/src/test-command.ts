// `pico-desktop test`: the contracts regenerated, then the frontend's Vitest and the backend's
// Pico tests. Both run even if the first fails; the result is the first failure.
//
// `pico-desktop test --e2e`: the end-to-end tests (the frontend's `e2e/**/*.e2e.ts`), in Vitest
// under Node, against the real app: the frontend built, the backend compiled, and each test's
// `launchApp()` starting the host with them (see e2e.ts).
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import { dirname, join } from 'node:path'
import { buildBackend } from './backend'
import { buildFrontend } from './build'
import type { ResolvedConfig } from './config'
import { E2E_SETUP, type E2eSetup } from './e2e'
import { generate } from './generate'
import { appArgs } from './host-args'
import type { Output } from './io'
import { passthrough } from './processes'
import { findHost } from './tools'

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

/** Where the end-to-end tests are, in the frontend. */
export const E2E_DIR = 'e2e'

/** Vitest's configuration for them: Node, one file at a time (each starts a window), long timeouts. */
export const E2E_VITEST_CONFIG = `// Written by pico-desktop test --e2e.
export default {
  test: {
    include: ['${E2E_DIR}/**/*.e2e.?(c|m)[jt]s'],
    environment: 'node',
    fileParallelism: false,
    testTimeout: 60000,
    hookTimeout: 60000,
  },
}
`

function hasE2eTests(dir: string): boolean {
  return existsSync(dir) && readdirSync(dir, { recursive: true, encoding: 'utf8' }).some(f => /\.e2e\.[cm]?[jt]s$/u.test(f))
}

/**
 * The backend `test --e2e` builds.
 *
 * @param out where it's built
 * @param platform the operating system
 * @returns its path
 */
function e2eBackend(out: string, platform: NodeJS.Platform): string {
  return join(out, platform === 'win32' ? 'backend.exe' : 'backend')
}

/**
 * The host's arguments for the app that `test --e2e` built (before `launchApp` adds its own).
 *
 * @param config the app
 * @param out where the frontend and backend were built
 * @param platform the operating system
 * @returns the arguments
 */
export function e2eHostArgs(config: ResolvedConfig, out: string, platform: NodeJS.Platform = process.platform): string[] {
  const app = [`--app-dir=${join(out, 'app')}`, `--app-id=${config.app.id}.e2e`, `--backend=${e2eBackend(out, platform)}`]

  return ['--dev', ...app, ...appArgs(config)]
}

/**
 * Runs the end-to-end tests against the real app.
 *
 * @param config the app
 * @param io where output goes
 * @returns 0 when all pass
 */
export async function testE2e(config: ResolvedConfig, io: Output): Promise<number> {
  const tests = join(config.frontend.root, E2E_DIR)
  if (!hasE2eTests(tests)) {
    io.err(`e2e │ no end-to-end tests: put them in ${tests}, named *.e2e.ts`)

    return 1
  }
  const host = findHost(config)
  if (!host) {
    io.err('e2e │ no pico-desktop-host: set PICO_DESKTOP_HOST or `host` in the config')

    return 1
  }
  generate(config)
  const out = join(config.root, '.pico-desktop', 'e2e')
  mkdirSync(out, { recursive: true })
  io.out('e2e │ building the frontend and the backend')
  await buildFrontend(config, join(out, 'app'))

  if (!(await buildBackend(config, e2eBackend(out, process.platform), io))) {
    return 1
  }
  const vitestConfig = join(out, 'vitest.config.mjs')
  writeFileSync(vitestConfig, E2E_VITEST_CONFIG)
  const setup: E2eSetup = { host, args: e2eHostArgs(config, out) }
  const env = { ...process.env, [E2E_SETUP]: JSON.stringify(setup) }

  const vitest = [vitestBin(config.frontend.root), 'run', '--config', vitestConfig, '--root', config.frontend.root]

  return passthrough(process.execPath, vitest, { cwd: config.frontend.root, io, prefix: 'e2e │ ', env })
}
