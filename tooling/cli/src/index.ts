// The pico-desktop CLI (PROJECT_DESCRIPTION.md §7): create, generate, dev, test, build and package.
import { parseArgs } from 'node:util'
import pkg from '../package.json' with { type: 'json' }
import { ConfigError, type ResolvedConfig, loadConfig } from './config'
import { create } from './create'
import { dev } from './dev'
import { doctor } from './doctor'
import { generate } from './generate'
import type { Output } from './io'
import { build } from './build'
import { packageApp } from './package-command'
import { test, testE2e } from './test-command'

export { CAPABILITIES, type Capability, type ContractConfig, type UserConfig, defineConfig } from './config'

export type { Output } from './io'

export { E2eApp, E2eError, type LaunchOptions, launchApp } from './e2e'

/** The product commands. */
export const COMMANDS = ['create', 'dev', 'generate', 'test', 'build', 'package', 'doctor'] as const

export type Command = typeof COMMANDS[number]

type Handler = (args: string[], io: Output) => Promise<number>

const usage = `Usage: pico-desktop <command> [options]

Commands:
  create <dir> [--id <app-id>] [--template vanilla-ts|vue-ts] [--framework <checkout>]   a new app
  dev [--trace-ipc] [--debug]   the app with HMR; Pico edits rebuild and restart the backend in the same window
             (--debug: for a debugger attached to the backend; see the template's launch.json)
  generate   the contracts → TypeScript and Pico
  test [--e2e]   Vitest and pico test (--e2e: the end-to-end tests, frontend/e2e/*.e2e.ts, in the real app)
  build      a portable bundle for this machine (dist/<name>-<version>-<target>/)
  package    that bundle as dist/<name>-<version>-<target>.tar.gz (+ .sha256), and an AppDir
  doctor     checks what development and builds need

Options:
  -h, --help     Show this help
  -V, --version  Show the version`

type AppCommand = (config: ResolvedConfig, io: Output) => Promise<number>

const inApp = (run: AppCommand): Handler => async (_args, io) => run(await loadConfig(process.cwd()), io)

const generateCommand: AppCommand = (config, io) => {
  const changed = generate(config)
  io.out(changed.length ? changed.map(f => `generated ${f}`).join('\n') : 'up to date')

  return Promise.resolve(0)
}

const createCommand: Handler = (args, io) => {
  const { positionals, values } = parseArgs({
    args,
    allowPositionals: true,
    options: { id: { type: 'string' }, framework: { type: 'string' }, template: { type: 'string' } },
  })
  if (positionals.length !== 1) {
    io.err('Usage: pico-desktop create <dir> [--id <app-id>] [--template vanilla-ts|vue-ts]\n  [--framework <checkout>]')

    return Promise.resolve(1)
  }

  const { id, framework, template } = values

  return Promise.resolve(create({ dir: positionals[0]!, id, framework, template }, io))
}

const say = (text: string): Handler => (_args, io) => {
  io.out(text)

  return Promise.resolve(0)
}

const handlers: Record<string, Handler> = {
  '-h': say(usage),
  '--help': say(usage),
  '-V': say(`pico-desktop ${pkg.version}`),
  '--version': say(`pico-desktop ${pkg.version}`),
  'create': createCommand,
  'dev': async (args, io) => {
    const { values } = parseArgs({ args, options: { 'trace-ipc': { type: 'boolean' }, 'debug': { type: 'boolean' } } })

    return dev(await loadConfig(process.cwd()), io, { traceIpc: values['trace-ipc'], debug: values.debug })
  },
  'generate': inApp(generateCommand),
  'doctor': inApp(doctor),
  'test': async (args, io) => {
    const { values } = parseArgs({ args, options: { e2e: { type: 'boolean' } } })
    const config = await loadConfig(process.cwd())

    return values.e2e ? testE2e(config, io) : test(config, io)
  },
  'build': inApp(build),
  'package': inApp(packageApp),
}

const unknown: Handler = ([first], io) => {
  io.err(first === undefined ? usage : `pico-desktop: unknown command '${first}'\n\n${usage}`)

  return Promise.resolve(1)
}

async function reportingConfigErrors(run: () => Promise<number>, io: Output): Promise<number> {
  try {
    return await run()
  } catch (error) {
    if (!(error instanceof ConfigError)) {
      throw error
    }
    io.err(`pico-desktop: ${error.message}`)

    return 1
  }
}

/**
 * Runs the CLI.
 *
 * @param args the arguments, without node and the script
 * @param io where output and errors go
 * @returns the exit code: 0 on success, 1 for a failure or a usage or configuration error
 */
export function main(args: string[], io: Output): Promise<number> {
  const [first, ...rest] = args
  const handler = first !== undefined && Object.hasOwn(handlers, first) ? handlers[first]! : null

  return handler ? reportingConfigErrors(() => handler(rest, io), io) : unknown(args, io)
}
