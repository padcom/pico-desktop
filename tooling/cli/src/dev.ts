// `pico-desktop dev`: Vite's dev server (HMR for the frontend), the host in development mode, and
// the Pico backend, rebuilt on every change and restarted in the same window. A build that fails
// leaves the running backend alone.
import { type ChildProcessWithoutNullStreams, spawn } from 'node:child_process'
import { type FSWatcher, watch } from 'node:fs'
import { basename, dirname } from 'node:path'
import { buildBackend, devBackend } from './backend'
import type { ResolvedConfig } from './config'
import { generate } from './generate'
import type { Output } from './io'
import { lines } from './processes'
import { Rebuilder } from './rebuilder'
import { findHost, findPico } from './tools'
import { startDevServer } from './vite'

/**
 * Whether a changed file (relative to the backend) makes a rebuild: Pico sources and manifests,
 * not tests.
 *
 * @param file the file
 * @returns whether to rebuild
 */
export function isBackendSource(file: string): boolean {
  return /\.pi$|(?:^|\/)project\.json$/u.test(file) && !file.endsWith('.test.pi')
}

function regenerate(config: ResolvedConfig, io: Output) {
  try {
    generate(config).forEach(file => io.out(`dev │ generated ${file}`))
  } catch (error) {
    io.err(`dev │ the contract is invalid: ${String(error)}`)
  }
}

function watchSources(config: ResolvedConfig, io: Output, rebuilder: Rebuilder): FSWatcher[] {
  const backend = watch(config.backend.root, { recursive: true }, (_event, file) => {
    if (file && isBackendSource(file)) {
      rebuilder.changed()
    }
  })
  const contracts = config.contracts.map(c => watch(dirname(c.source), (_event, file) => {
    if (file === basename(c.source)) {
      regenerate(config, io)
    }
  }))

  return [backend, ...contracts]
}

/** Chromium's log lines that say nothing to an app developer. */
const NOISE = [/GetVSyncParametersIfAvailable\(\) failed/u]

/**
 * Whether a line of the host's output is worth showing.
 *
 * @param line the line
 * @returns false for known Chromium noise
 */
export function worthShowing(line: string): boolean {
  return !NOISE.some(pattern => pattern.test(line))
}

export interface DevOptions {
  /** Log every message's envelope (not its payload) in the host. */
  traceIpc?: boolean | undefined
}

function startHost(host: string, config: ResolvedConfig, url: string, options: DevOptions & { io: Output }) {
  const { io } = options
  const args = ['--dev', `--dev-url=${url}`, `--app-id=${config.app.id}`, `--backend=${devBackend(config)}`, '--control=stdin']
  if (options.traceIpc) {
    args.push('--trace-ipc')
  }
  // Its own process group: a terminal's Ctrl+C reaches only the CLI, which then quits the host
  // (and through it the backend) in order, with the backend's grace period.
  const child = spawn(host, args, { cwd: config.root, stdio: ['pipe', 'pipe', 'pipe'], detached: true })
  lines(child.stdout, line => io.out(`host │ ${line}`))
  lines(child.stderr, line => worthShowing(line) && io.err(`host │ ${line}`))

  return child as ChildProcessWithoutNullStreams
}

const NO_HOST = 'no pico-desktop-host: set PICO_DESKTOP_HOST or `host` in the config (or run `pnpm cef` in the framework)'
const NO_PICO = 'no pico on the PATH: install Pico (https://github.com/padcom/pico-lang)'

/**
 * Checks the tools; reports what's missing.
 *
 * @param config the app
 * @param io where problems go
 * @returns the host, or null if something's missing
 */
async function preflight(config: ResolvedConfig, io: Output): Promise<string | null> {
  const host = findHost(config)
  const pico = await findPico()
  const problems = [pico ? pico.warning : NO_PICO, host ? null : NO_HOST]
  problems.filter(p => p !== null).forEach(p => io.err(`dev │ ${p}`))

  return pico ? host : null
}

const exited = (child: ChildProcessWithoutNullStreams) => new Promise<number>(resolve => {
  child.on('close', code => resolve(code ?? 1))
})

/**
 * Runs the app in development until its window closes (or Ctrl+C).
 *
 * @param config the app
 * @param io where output goes
 * @param options development options
 * @returns the host's exit code
 */
export async function dev(config: ResolvedConfig, io: Output, options: DevOptions = {}): Promise<number> {
  const host = await preflight(config, io)
  if (host === null) {
    return 1
  }
  regenerate(config, io)
  const built = await buildBackend(config, devBackend(config), io)
  io.out(built ? 'dev │ built the backend' : 'dev │ the backend has errors: fix them and save (the window opens anyway)')
  const server = await startDevServer(config.frontend.root)
  const child = startHost(host, config, server.url, { ...options, io })
  const rebuilder = new Rebuilder({
    build: () => buildBackend(config, devBackend(config), io),
    built: () => {
      io.out('dev │ rebuilt the backend: restarting it')
      child.stdin.write('restart-backend\n')
    },
  })
  const watchers = watchSources(config, io, rebuilder)
  const quit = () => child.stdin.write('quit\n')
  const signals = ['SIGINT', 'SIGTERM', 'SIGHUP'] as const
  signals.forEach(s => process.on(s, quit))
  const code = await exited(child)
  signals.forEach(s => process.off(s, quit))
  rebuilder.stop()
  watchers.forEach(w => w.close())
  await server.close()

  return code
}
