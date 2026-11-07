// The host's options that come from the app's configuration, for `dev` and the bundle's launcher.
import type { ResolvedConfig } from './config'

/**
 * The host's flags for the app's capabilities, window and close grace period.
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

  return args
}

/**
 * An argument quoted for a POSIX shell.
 *
 * @param arg the argument
 * @returns it in single quotes
 */
export const shellQuote = (arg: string) => `'${arg.replaceAll("'", "'\\''")}'`
