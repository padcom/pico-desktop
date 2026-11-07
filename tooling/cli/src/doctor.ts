// `pico-desktop doctor`: what the app's development and builds need, checked at once, with what
// to do about each problem (docs/troubleshooting.md has more).
import { readFileSync } from 'node:fs'
import type { ResolvedConfig } from './config'
import type { Output } from './io'
import { capture } from './processes'
import { currentTarget, locateRuntime } from './runtime'
import { compareVersions, findHost, findPico } from './tools'

/** `error`: development can't work; `warning`: it can, but not everything. */
export type Severity = 'ok' | 'warning' | 'error'

export interface Finding {
  severity: Severity
  what: string
  fix?: string | undefined
}

/** Node: the version from the framework's toolchain. */
export const MINIMUM_NODE = '26.9.0'

const finding = (severity: Severity, what: string, fix?: string): Finding => ({ severity, what, fix })

function nodeFinding(version: string): Finding {
  return compareVersions(version.replace(/^v/u, ''), MINIMUM_NODE) >= 0
    ? finding('ok', `Node ${version}`)
    : finding('error', `Node ${version}`, `use Node ${MINIMUM_NODE} or newer (nvm install)`)
}

async function picoFinding(): Promise<Finding> {
  const pico = await findPico()
  if (!pico) {
    return finding('error', 'no Pico compiler on the PATH', 'install Pico (https://github.com/padcom/pico-lang)')
  }

  return pico.warning ? finding('warning', `pico ${pico.version}`, pico.warning) : finding('ok', `pico ${pico.version}`)
}

function hostFinding(config: ResolvedConfig): Finding {
  const host = findHost(config)

  return host
    ? finding('ok', `host for dev: ${host}`)
    : finding('error', 'no pico-desktop-host for dev', 'set PICO_DESKTOP_HOST or `host` in the config')
}

function runtimeFinding(config: ResolvedConfig): Finding {
  const target = currentTarget()
  if (!target) {
    return finding('warning', `no runtime for ${process.platform}-${process.arch} yet`, 'build and package on linux')
  }
  try {
    return finding('ok', `runtime for build: ${locateRuntime(config.runtime, target)}`)
  } catch (error) {
    return finding('warning', 'no runtime for build', (error as Error).message)
  }
}

function readSysctl(path: string): string | null {
  try {
    return readFileSync(path, 'utf8').trim()
  } catch {
    return null
  }
}

function namespaceFix(): string {
  const restricted = readSysctl('/proc/sys/kernel/apparmor_restrict_unprivileged_userns') === '1'

  return restricted
    ? 'sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0, or an AppArmor profile (docs/platform-linux.md)'
    : 'sudo sysctl -w kernel.unprivileged_userns_clone=1 (docs/platform-linux.md)'
}

/**
 * Whether Chromium's sandbox can use unprivileged user namespaces here (Linux).
 *
 * @returns the finding
 */
async function userNamespacesFinding(): Promise<Finding> {
  if (process.platform !== 'linux') {
    return finding('ok', 'sandbox: not checked off Linux')
  }
  const { code } = await capture('unshare', ['--user', 'true'], process.cwd())
  if (code === 127) {
    return finding('warning', 'sandbox: unshare is missing, so user namespaces weren\'t checked')
  }

  return code === 0
    ? finding('ok', 'sandbox: unprivileged user namespaces work')
    : finding('error', 'sandbox: unprivileged user namespaces are disabled', namespaceFix())
}

/**
 * Every check.
 *
 * @param config the app
 * @returns the findings, in order
 */
export async function diagnose(config: ResolvedConfig): Promise<Finding[]> {
  return [nodeFinding(process.version),
    await picoFinding(),
    hostFinding(config),
    runtimeFinding(config),
    await userNamespacesFinding()]
}

const MARKS: Record<Severity, string> = { ok: '✓', warning: '!', error: '✗' }

/**
 * `pico-desktop doctor`.
 *
 * @param config the app
 * @param io where the report goes
 * @returns 0 unless something development needs is missing
 */
export async function doctor(config: ResolvedConfig, io: Output): Promise<number> {
  const findings = await diagnose(config)
  findings.forEach(f => io.out(`${MARKS[f.severity]} ${f.what}${f.fix ? `\n    → ${f.fix}` : ''}`))

  return findings.some(f => f.severity === 'error') ? 1 : 0
}
