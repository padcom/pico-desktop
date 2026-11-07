// The host's options that come from the app's configuration, for `dev` and the bundle's launcher.
import { DEFAULT_LIVENESS, type ResolvedConfig } from './config'

/**
 * The host's liveness flag, when the app changes the default.
 *
 * @param config the app
 * @returns the flag, or none
 */
function livenessArgs(config: ResolvedConfig): string[] {
  const l = config.liveness
  if (l === false) {
    return ['--liveness=off']
  }

  return l.intervalMs === DEFAULT_LIVENESS.intervalMs && l.timeoutMs === DEFAULT_LIVENESS.timeoutMs
    ? []
    : [`--liveness=${l.intervalMs}/${l.timeoutMs}`]
}

/**
 * The host's flags for the app's capabilities, window, close grace period and liveness.
 *
 * @param config the app
 * @returns the flags (each a whole argument)
 */
export function appArgs(config: ResolvedConfig): string[] {
  const { capabilities, window } = config
  const args = [`--window-size=${window.width}x${window.height}`, `--close-grace=${config.closeGraceMs}`]
  if (capabilities.length > 0) {
    args.push(`--capabilities=${capabilities.join(',')}`)
  }
  if (window.title) {
    args.push(`--window-title=${window.title}`)
  }

  return [...args, ...livenessArgs(config)]
}

/**
 * An argument quoted for a POSIX shell.
 *
 * @param arg the argument
 * @returns it in single quotes
 */
export const shellQuote = (arg: string) => `'${arg.replaceAll("'", "'\\''")}'`
