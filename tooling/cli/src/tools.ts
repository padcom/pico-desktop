// The tools the app needs: the pico compiler (on the PATH) and the pico-desktop-host executable.
import { existsSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import type { ResolvedConfig } from './config'
import { capture } from './processes'

/** The oldest pico the framework works with (pico-lang #82: stdout reaches a pipe at once). */
export const MINIMUM_PICO = '0.4.2'

/**
 * Compares two `x.y.z` versions.
 *
 * @param a one version
 * @param b the other
 * @returns negative, zero or positive, as `a` is older, the same or newer
 */
export function compareVersions(a: string, b: string): number {
  const parts = (v: string) => v.split('.').map(n => Number.parseInt(n, 10) || 0)
  const [x, y] = [parts(a), parts(b)]
  const differs = [0, 1, 2].find(i => (x[i] ?? 0) !== (y[i] ?? 0))

  return differs === undefined ? 0 : (x[differs] ?? 0) - (y[differs] ?? 0)
}

export interface Pico {
  version: string
  /** Set when the version is older than MINIMUM_PICO (a local build may still have the fixes). */
  warning: string | null
}

function warningFor(version: string): string | null {
  return compareVersions(version, MINIMUM_PICO) < 0
    ? `pico ${version} is older than ${MINIMUM_PICO}; use a pico-lang build with the fixes`
    : null
}

/**
 * Finds pico on the PATH.
 *
 * @returns its version (and a warning if it's old), or null if there's none
 */
export async function findPico(): Promise<Pico | null> {
  const { code, output } = await capture('pico', ['--version'], process.cwd())
  // The Pico compiler says `pico x.y.z`; Debian's `pico` is nano.
  const version = code === 0 ? /^pico (?<version>\d+\.\d+\.\d+)/mu.exec(output)?.groups?.version : undefined

  return version === undefined ? null : { version, warning: warningFor(version) }
}

/** The host from the framework's own build (when the CLI runs from the framework's sources). */
const FRAMEWORK_HOST = fileURLToPath(
  new URL(/* @vite-ignore */ '../../../build/cef/runtime/cef-host/pico-desktop-host', import.meta.url),
)

/**
 * The host executable: the config's `host`, $PICO_DESKTOP_HOST, or the framework's build.
 *
 * @param config the app
 * @param env the environment
 * @returns its path, or null if none exists
 */
export function findHost(config: ResolvedConfig, env: NodeJS.ProcessEnv = process.env): string | null {
  const candidates = [config.host, env.PICO_DESKTOP_HOST, FRAMEWORK_HOST]

  return candidates.find((p): p is string => p !== undefined && p !== '' && existsSync(p)) ?? null
}
