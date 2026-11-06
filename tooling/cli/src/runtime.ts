// The host runtime an app's bundle carries (scripts/stage-runtime.mjs in the framework makes it):
// pico-desktop-host and CEF's files, with runtime.json listing every file's SHA-256. It's used
// only after every file checks out, so a tampered or incomplete runtime is refused by name.
import { createHash } from 'node:crypto'
import { existsSync, mkdirSync, readFileSync, readdirSync, renameSync, rmSync, statSync, writeFileSync } from 'node:fs'
import { homedir } from 'node:os'
import { basename, join, relative } from 'node:path'
import { fileURLToPath } from 'node:url'
import { capture } from './processes'

export interface RuntimeManifest {
  name: string
  version: string
  target: string
  host: string
  cef: { version: string, chromium: string }
  protocol: number
  pico: { minimum: string }
  files: Record<string, string>
}

export class RuntimeError extends Error {}

/** The protocol the apps this CLI builds speak (docs/protocol.md). */
export const PROTOCOL = 1

const TARGETS: Record<string, string> = { 'linux-arm64': 'linux/arm64', 'linux-x64': 'linux/x64' }

/**
 * This machine's target (`linux-arm64`, …).
 *
 * @returns the target, or null where there's no runtime yet
 */
export function currentTarget(): string | null {
  const here = `${process.platform}/${process.arch}`

  return Object.keys(TARGETS).find(t => TARGETS[t] === here) ?? null
}

const sha256 = (path: string) => createHash('sha256').update(readFileSync(path)).digest('hex')

function filesIn(dir: string): string[] {
  return readdirSync(dir).flatMap(entry => {
    const full = join(dir, entry)

    return statSync(full).isDirectory() ? filesIn(full) : [full]
  })
}

function readManifest(dir: string): RuntimeManifest {
  const path = join(dir, 'runtime.json')
  if (!existsSync(path)) {
    throw new RuntimeError(`${dir} isn't a Pico Desktop runtime (no runtime.json)`)
  }

  return JSON.parse(readFileSync(path, 'utf8')) as RuntimeManifest
}

function checkHeader(dir: string, manifest: RuntimeManifest, target: string) {
  if (manifest.target !== target) {
    throw new RuntimeError(`${dir} is a runtime for ${manifest.target}, not ${target}`)
  }
  if (manifest.protocol !== PROTOCOL) {
    throw new RuntimeError(`${dir} speaks protocol ${manifest.protocol}; this CLI's apps speak ${PROTOCOL}: use a matching one`)
  }
}

/**
 * Checks a runtime directory against its manifest: the target, then every file's SHA-256, and no
 * files it doesn't list.
 *
 * @param dir the runtime
 * @param target the target it must be for
 * @returns its manifest
 */
export function verifyRuntime(dir: string, target: string): RuntimeManifest {
  const manifest = readManifest(dir)
  checkHeader(dir, manifest, target)
  const listed = Object.entries(manifest.files)
  const bad = listed.find(([file, hash]) => !existsSync(join(dir, file)) || sha256(join(dir, file)) !== hash)
  if (bad) {
    throw new RuntimeError(`the runtime in ${dir} was changed or is incomplete: ${bad[0]} doesn't match runtime.json`)
  }
  const extra = filesIn(dir).map(f => relative(dir, f)).find(f => f !== 'runtime.json' && !(f in manifest.files))
  if (extra) {
    throw new RuntimeError(`the runtime in ${dir} has a file runtime.json doesn't list: ${extra}`)
  }

  return manifest
}

/**
 * Where unpacked runtimes are kept.
 *
 * @param env the environment
 * @returns `$XDG_CACHE_HOME/pico-desktop/runtimes` (or under ~/.cache)
 */
export function runtimeCache(env: NodeJS.ProcessEnv = process.env): string {
  return join(env.XDG_CACHE_HOME ?? join(homedir(), '.cache'), 'pico-desktop', 'runtimes')
}

function checkArchive(archive: string) {
  const sidecar = `${archive}.sha256`
  if (!existsSync(sidecar)) {
    throw new RuntimeError(`${archive} has no ${basename(sidecar)} to check it against`)
  }
  if (readFileSync(sidecar, 'utf8').split(/\s/u)[0] !== sha256(archive)) {
    throw new RuntimeError(`${archive} doesn't match its SHA-256 (${basename(sidecar)}): refusing it`)
  }
}

/**
 * Unpacks a runtime archive (`<name>.tar.gz`, checked against `<name>.tar.gz.sha256` next to it)
 * into the cache, once; later builds reuse it offline.
 *
 * @param archive the archive
 * @param cache the cache directory
 * @returns the unpacked runtime's directory
 */
export async function unpackRuntime(archive: string, cache: string): Promise<string> {
  const name = basename(archive).replace(/\.tar\.gz$/u, '')
  const dir = join(cache, name)
  if (existsSync(join(dir, 'runtime.json'))) {
    return dir
  }
  checkArchive(archive)
  mkdirSync(cache, { recursive: true })
  const staging = join(cache, `.${name}.unpacking`)
  rmSync(staging, { recursive: true, force: true })
  mkdirSync(staging)
  const { code, output } = await capture('tar', ['-xzf', archive, '-C', staging], cache)
  if (code !== 0) {
    throw new RuntimeError(`couldn't unpack ${archive}: ${output.trim()}`)
  }
  renameSync(join(staging, name), dir)
  rmSync(staging, { recursive: true, force: true })

  return dir
}

async function download(url: string, to: string) {
  let response: Response
  try {
    response = await fetch(url)
  } catch (error) {
    throw new RuntimeError(`couldn't download ${url} (${String(error)}); a runtime used before is reused offline`)
  }

  if (!response.ok) {
    throw new RuntimeError(`couldn't download ${url}: HTTP ${response.status}`)
  }
  writeFileSync(to, Buffer.from(await response.arrayBuffer()))
}

/**
 * Downloads a runtime archive and its .sha256 into the cache (once: a cached, unpacked runtime is
 * reused offline) and unpacks it.
 *
 * @param url the archive's URL (`…/<name>.tar.gz`; `<url>.sha256` next to it)
 * @param cache the cache directory
 * @returns the unpacked runtime's directory
 */
export async function fetchRuntime(url: string, cache: string): Promise<string> {
  const file = join(cache, 'downloads', basename(new URL(url).pathname))
  if (!existsSync(join(cache, basename(file).replace(/\.tar\.gz$/u, ''), 'runtime.json'))) {
    mkdirSync(join(cache, 'downloads'), { recursive: true })
    await download(url, file)
    await download(`${url}.sha256`, `${file}.sha256`)
  }

  return unpackRuntime(file, cache)
}

/** The framework checkout's staged runtimes (when the CLI runs from a checkout). */
const FRAMEWORK_RUNTIMES = fileURLToPath(new URL(/* @vite-ignore */ '../../../build/runtime', import.meta.url))

function stagedRuntime(target: string): string {
  const staged = existsSync(FRAMEWORK_RUNTIMES)
    ? readdirSync(FRAMEWORK_RUNTIMES).filter(f => f.endsWith(`-${target}`)).sort().at(-1)
    : undefined
  if (!staged) {
    throw new RuntimeError(`no Pico Desktop runtime for ${target}: set PICO_DESKTOP_RUNTIME (or \`runtime\` in the config) to `
      + 'a runtime directory or archive, or run `pnpm cef && pnpm runtime` in a framework checkout')
  }

  return join(FRAMEWORK_RUNTIMES, staged)
}

/**
 * The runtime to build with: `runtime` in the config or $PICO_DESKTOP_RUNTIME (a directory, a
 * .tar.gz, or an https:// URL of one), else the framework checkout's staged one for this target.
 *
 * @param configured the config's `runtime` (absolute), if any
 * @param target this machine's target
 * @param env the environment
 * @returns a directory or an archive
 */
export function locateRuntime(configured: string | undefined, target: string, env: NodeJS.ProcessEnv = process.env): string {
  return configured ?? env.PICO_DESKTOP_RUNTIME ?? stagedRuntime(target)
}
