// The app's configuration (`pico-desktop.config.ts` at its root) and what the CLI derives from
// it: absolute paths, defaults for the standard layout, the app ID checked like the host checks it.
import { existsSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { pathToFileURL } from 'node:url'

export const CONFIG_FILE = 'pico-desktop.config.ts'

export interface ContractConfig {
  /** The CDDL contract. */
  source: string
  /** The generated TypeScript module (types, validators, `Services`, `validate`). */
  typescript: string
  /** The generated Pico module (structs, decoders, encoders). */
  pico: string
}

/** The host's native services the backend may use (docs: packages/communicator-pico/host.pi). */
export const CAPABILITIES = ['window', 'dialogs', 'notifications'] as const

export type Capability = typeof CAPABILITIES[number]

export interface UserConfig {
  app: { id: string, name?: string, version?: string }
  /** The native services the backend may call (anything else fails with NOT_ALLOWED). */
  capabilities?: Capability[]
  /** The window: its initial size (1024×768) and a fixed title (default: the page's <title>). */
  window?: { title?: string, width?: number, height?: number }
  /** How long the backend has to finish when the app closes, in ms (3000). */
  closeGraceMs?: number
  /**
   * How the host checks the backend is alive: a ping every `intervalMs` (5000), and a hang after
   * `timeoutMs` without an answer (15000), which restarts the backend. `false`: no pings, for a
   * backend that may block its event loop on purpose for long (prefer making it not block).
   */
  liveness?: { intervalMs: number, timeoutMs: number } | false
  /** The app's icon for its AppImage (.png or .svg; a plain one otherwise). */
  icon?: string
  frontend?: { root?: string }
  backend?: { root?: string, entry?: string }
  contracts?: ContractConfig[]
  /** The pico-desktop-host executable (default: $PICO_DESKTOP_HOST, then the framework's build). */
  host?: string
  /** The runtime `build` bundles: a directory or a .tar.gz (default: $PICO_DESKTOP_RUNTIME, then the framework's). */
  runtime?: string
}

export interface ResolvedConfig {
  root: string
  app: { id: string, name: string, version: string }
  capabilities: Capability[]
  window: { title?: string | undefined, width: number, height: number }
  closeGraceMs: number
  liveness: { intervalMs: number, timeoutMs: number } | false
  icon: string | undefined
  frontend: { root: string }
  backend: { root: string, entry: string }
  contracts: ContractConfig[]
  host: string | undefined
  runtime: string | undefined
}

/**
 * Types the configuration (`export default defineConfig({ … })`).
 *
 * @param config the configuration
 * @returns it, unchanged
 */
export const defineConfig = (config: UserConfig): UserConfig => config

const APP_ID = /^[a-z0-9][a-z0-9-]*(?:\.[a-z0-9][a-z0-9-]*)*$/u

export class ConfigError extends Error {}

const DEFAULT_CONTRACTS: ContractConfig[] = [{
  source: 'packages/contracts/app.cddl',
  typescript: 'apps/frontend/src/generated/app.ts',
  pico: 'apps/backend/generated/app.pi',
}]

function optionalPath(root: string, path: string | undefined): string | undefined {
  return path === undefined ? undefined : resolve(root, path)
}

function runtimeLocation(root: string, runtime: string | undefined): string | undefined {
  if (runtime === undefined || /^https?:\/\//u.test(runtime)) {
    return runtime
  }

  return resolve(root, runtime)
}

function checkedCapabilities(user: UserConfig): Capability[] {
  const given: unknown[] = user.capabilities ?? []
  const unknown = given.find(c => !(CAPABILITIES as readonly unknown[]).includes(c))
  if (unknown !== undefined) {
    throw new ConfigError(`capability '${String(unknown)}' is unknown: use ${CAPABILITIES.join(', ')}`)
  }

  return [...new Set(given as Capability[])]
}

function checkedId(user: UserConfig): string {
  const id: unknown = (user as Partial<UserConfig>).app?.id
  if (typeof id !== 'string' || !APP_ID.test(id)) {
    throw new ConfigError(`app.id '${String(id)}' is invalid: use lowercase letters, digits, '-' and '.' (like com.example.app)`)
  }

  return id
}

/** The host's liveness defaults (docs/protocol.md §7). */
export const DEFAULT_LIVENESS = { intervalMs: 5000, timeoutMs: 15000 }

/**
 * The liveness timings, checked as the host checks them.
 *
 * @param user the configuration as written
 * @returns the timings, or false for none
 */
function checkedLiveness(user: UserConfig): ResolvedConfig['liveness'] {
  const l = user.liveness ?? DEFAULT_LIVENESS
  const valid = (t: { intervalMs: number, timeoutMs: number }) => t.intervalMs >= 100 && t.timeoutMs > t.intervalMs
  if (l !== false && !valid(l)) {
    const got = `${l.intervalMs}/${l.timeoutMs}`
    throw new ConfigError(`liveness: intervalMs must be at least 100 and timeoutMs longer than it (got ${got})`)
  }

  return l
}

/**
 * Fills in the defaults and makes every path absolute.
 *
 * @param root the app's root directory
 * @param user the configuration as written
 * @returns the resolved configuration
 */
export function resolveConfig(root: string, user: UserConfig): ResolvedConfig {
  const id = checkedId(user)
  const at = (path: string) => resolve(root, path)
  const frontend = { root: 'apps/frontend', ...user.frontend }
  const backend = { root: 'apps/backend', entry: 'main.pi', ...user.backend }
  const contracts = (user.contracts ?? DEFAULT_CONTRACTS).map(c => ({
    source: at(c.source), typescript: at(c.typescript), pico: at(c.pico),
  }))

  return {
    root,
    app: { name: id, version: '0.0.0', ...user.app },
    capabilities: checkedCapabilities(user),
    window: { width: 1024, height: 768, ...user.window },
    closeGraceMs: user.closeGraceMs ?? 3000,
    liveness: checkedLiveness(user),
    icon: optionalPath(root, user.icon),
    frontend: { root: at(frontend.root) },
    backend: { root: at(backend.root), entry: backend.entry },
    contracts,
    host: optionalPath(root, user.host),
    runtime: runtimeLocation(root, user.runtime),
  }
}

/**
 * The app's root: the nearest directory with a `pico-desktop.config.ts`, from `from` up.
 *
 * @param from where to start
 * @returns the directory, or null outside an app
 */
export function findRoot(from: string): string | null {
  const dir = resolve(from)
  if (existsSync(join(dir, CONFIG_FILE))) {
    return dir
  }

  return dirname(dir) === dir ? null : findRoot(dirname(dir))
}

/**
 * Loads the app's configuration (Node runs the TypeScript file directly).
 *
 * @param from a directory inside the app
 * @returns the resolved configuration
 */
export async function loadConfig(from: string): Promise<ResolvedConfig> {
  const root = findRoot(from)
  if (root === null) {
    throw new ConfigError(`no ${CONFIG_FILE} here or above (run pico-desktop create first)`)
  }
  const module = await import(pathToFileURL(join(root, CONFIG_FILE)).href) as { default?: UserConfig }
  if (!module.default) {
    throw new ConfigError(`${CONFIG_FILE} must export default defineConfig({ … })`)
  }

  return resolveConfig(root, module.default)
}
