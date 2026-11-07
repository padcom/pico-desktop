// THIRD-PARTY-NOTICES.txt for a bundle: the license texts of the frontend's production
// dependencies, direct and transitive (MIT, BSD, Apache-2.0 and most others require shipping
// them). The runtime's own licenses (CEF, Chromium) are in runtime/licenses.
import { existsSync, readFileSync, readdirSync, realpathSync } from 'node:fs'
import { dirname, join } from 'node:path'

export interface Package {
  name: string
  version: string
  license?: string | undefined
  /** The license and notice files' text, or null if the package has none. */
  text: string | null
}

interface Manifest {
  version?: string
  license?: string
  dependencies?: Record<string, string>
  optionalDependencies?: Record<string, string>
}

interface Installed {
  dir: string
  manifest: Manifest
}

const LICENSE_FILE = /^(?:licen[cs]e|copying|notice)(?:\.|-|$)/iu

const readManifest = (dir: string) => JSON.parse(readFileSync(join(dir, 'package.json'), 'utf8')) as Manifest

/**
 * Where `name` is installed as seen from `dir` (node_modules up the tree, as Node looks), with
 * symlinks resolved (pnpm links packages into its store).
 *
 * @param dir where to start
 * @param name the package
 * @returns its directory and manifest, or null if it isn't installed
 */
function installed(dir: string, name: string): Installed | null {
  const candidate = join(dir, 'node_modules', name)
  if (existsSync(join(candidate, 'package.json'))) {
    const real = realpathSync(candidate)

    return { dir: real, manifest: readManifest(real) }
  }

  return dirname(dir) === dir ? null : installed(dirname(dir), name)
}

/**
 * The package's license and notice files, joined.
 *
 * @param dir the package's directory
 * @returns the text, or null if there are none
 */
function licenseText(dir: string): string | null {
  const texts = readdirSync(dir).filter(f => LICENSE_FILE.test(f)).sort()
    .map(f => readFileSync(join(dir, f), 'utf8').trim())
    .filter(t => t !== '')

  return texts.length === 0 ? null : texts.join('\n\n')
}

/**
 * An installed package as a notices entry.
 *
 * @param name its name
 * @param found where it is and its manifest
 * @returns the entry
 */
function entry(name: string, found: Installed): Package {
  return { name, version: found.manifest.version ?? 'unknown', license: found.manifest.license, text: licenseText(found.dir) }
}

/**
 * Adds `deps`, as installed from `from`, and what they depend on, to `seen`.
 *
 * @param seen the packages so far, by name@version
 * @param from the directory the dependencies are looked up from
 * @param deps the dependencies (name → range)
 */
function visit(seen: Map<string, Package>, from: string, deps: Record<string, string>) {
  for (const name of Object.keys(deps)) {
    const found = installed(from, name)
    if (found === null) {
      continue
    }
    const p = entry(name, found)
    if (!seen.has(`${p.name}@${p.version}`)) {
      seen.set(`${p.name}@${p.version}`, p)
      visit(seen, found.dir, { ...found.manifest.dependencies, ...found.manifest.optionalDependencies })
    }
  }
}

/**
 * The frontend's production dependencies, transitively, each once (by name and version).
 *
 * @param frontend the frontend's root (its package.json)
 * @returns them, sorted by name
 */
export function productionPackages(frontend: string): Package[] {
  const seen = new Map<string, Package>()
  if (existsSync(join(frontend, 'package.json'))) {
    visit(seen, frontend, readManifest(frontend).dependencies ?? {})
  }

  return [...seen.values()].sort((a, b) => a.name.localeCompare(b.name) || a.version.localeCompare(b.version))
}

/**
 * The notices file's text.
 *
 * @param app the app's name
 * @param packages the frontend's production packages
 * @returns the text
 */
export function notices(app: string, packages: Package[]): string {
  const parts = [
    `Third-party software in ${app}`,
    '',
    'The frontend includes the packages below; their license texts follow. The runtime\'s',
    'licenses (CEF, Chromium, Pico Desktop) are in runtime/licenses. The Pico backend\'s',
    'statically linked runtime libraries aren\'t listed yet (pico-lang has no way to report',
    'them).',
  ]
  for (const p of packages) {
    parts.push('', '-'.repeat(78), `${p.name} ${p.version}${p.license ? ` (${p.license})` : ''}`, '-'.repeat(78), '')
    parts.push(p.text ?? '(The package has no license file; its declared license is above.)')
  }

  return `${parts.join('\n')}\n`
}
