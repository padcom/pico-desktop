// The pico-desktop CLI (PROJECT_DESCRIPTION.md §7): create, generate, dev and test; build and
// package come with F6.
import { parseArgs } from 'node:util'
import pkg from '../package.json' with { type: 'json' }
import { ConfigError, type ResolvedConfig, loadConfig } from './config'
import { create } from './create'
import { dev } from './dev'
import { generate } from './generate'
import type { Output } from './io'
import { test } from './test-command'

export { type ContractConfig, type UserConfig, defineConfig } from './config'

export type { Output } from './io'

/** The product commands. */
export const COMMANDS = ['create', 'dev', 'generate', 'test', 'build', 'package'] as const

export type Command = typeof COMMANDS[number]

type Handler = (args: string[], io: Output) => Promise<number>

const usage = `Usage: pico-desktop <command> [options]

Commands:
  create <dir> [--id <app-id>] [--framework <checkout>]   a new app
  dev [--trace-ipc]   the app with HMR; Pico edits rebuild and restart the backend in the same window
  generate   the contracts → TypeScript and Pico
  test       Vitest and pico test
  build      (F6)
  package    (F6)

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
    args, allowPositionals: true, options: { id: { type: 'string' }, framework: { type: 'string' } },
  })
  if (positionals.length !== 1) {
    io.err('Usage: pico-desktop create <dir> [--id <app-id>] [--framework <checkout>]')

    return Promise.resolve(1)
  }

  return Promise.resolve(create({ dir: positionals[0]!, id: values.id, framework: values.framework }, io))
}

const later = (command: string): Handler => (_args, io) => {
  io.err(`pico-desktop ${command}: not implemented yet (see PLAN.md, phase F6)`)

  return Promise.resolve(2)
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
    const { values } = parseArgs({ args, options: { 'trace-ipc': { type: 'boolean' } } })

    return dev(await loadConfig(process.cwd()), io, { traceIpc: values['trace-ipc'] })
  },
  'generate': inApp(generateCommand),
  'test': inApp(test),
  'build': later('build'),
  'package': later('package'),
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
 * @returns the exit code: 0 on success, 1 for a usage or configuration error, 2 for a command that isn't implemented yet
 */
export function main(args: string[], io: Output): Promise<number> {
  const [first, ...rest] = args
  const handler = first !== undefined && Object.hasOwn(handlers, first) ? handlers[first]! : null

  return handler ? reportingConfigErrors(() => handler(rest, io), io) : unknown(args, io)
}
