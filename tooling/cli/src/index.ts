import pkg from '../package.json' with { type: 'json' }

/** The product commands (PROJECT_DESCRIPTION.md §7). */
export const COMMANDS = ['create', 'dev', 'test', 'build', 'package'] as const

export type Command = typeof COMMANDS[number]

export interface Output {
  out(text: string): void
  err(text: string): void
}

type Handler = (io: Output) => number

const usage = `Usage: pico-desktop <command> [options]

Commands:
${COMMANDS.map(c => `  ${c}`).join('\n')}

Options:
  -h, --help     Show this help
  -V, --version  Show the version`

const help: Handler = io => {
  io.out(usage)

  return 0
}

const version: Handler = io => {
  io.out(`pico-desktop ${pkg.version}`)

  return 0
}

const notImplemented = (command: Command): Handler => io => {
  io.err(`pico-desktop ${command}: not implemented yet (see PLAN.md, phase F4)`)

  return 2
}

const handlers: Record<string, Handler> = {
  '-h': help,
  '--help': help,
  '-V': version,
  '--version': version,
  ...Object.fromEntries(COMMANDS.map(command => [command, notImplemented(command)])),
}

/**
 * Runs the CLI.
 *
 * @param args the arguments, without node and the script
 * @param io where output and errors go
 * @returns the exit code: 0 on success, 1 for a usage error, 2 for a command that isn't implemented yet
 */
export function main(args: string[], io: Output): number {
  const [first] = args
  if (first === undefined) {
    io.out(usage)

    return 1
  }
  const handler = Object.hasOwn(handlers, first) ? handlers[first] : undefined
  if (handler) {
    return handler(io)
  }
  io.err(`pico-desktop: unknown command '${first}'\n\n${usage}`)

  return 1
}
