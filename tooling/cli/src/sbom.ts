// The bundle's software bill of materials (CycloneDX 1.5 JSON, sbom.cdx.json): the app, the
// runtime it carries (pico-desktop-host, CEF, Chromium), the Pico compiler that built its backend,
// and the frontend's runtime dependencies with their licenses.
import { existsSync, readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import type { ResolvedConfig } from './config'
import type { RuntimeManifest } from './runtime'

export interface Dependency {
  name: string
  version: string
  license?: string | undefined
}

interface Component {
  'type': string
  'name': string
  'version': string
  'licenses'?: { license: { id?: string, name?: string } }[]
  'bom-ref'?: string
}

const license = (id: string | undefined) => (id ? [{ license: /^[A-Za-z0-9.+-]+$/u.test(id) ? { id } : { name: id } }] : [])

/**
 * The SBOM.
 *
 * @param config the app
 * @param runtime the runtime the bundle carries
 * @param pico the Pico compiler's version, if known
 * @param dependencies the frontend's runtime dependencies
 * @returns the CycloneDX document
 */
export function sbom(config: ResolvedConfig, runtime: RuntimeManifest, pico: string | null, dependencies: Dependency[]) {
  const components: Component[] = [
    { type: 'framework', name: 'pico-desktop-runtime', version: runtime.version, licenses: license('MIT') },
    { type: 'library', name: 'cef', version: runtime.cef.version, licenses: license('BSD-3-Clause') },
    {
      type: 'library', name: 'chromium', version: runtime.cef.chromium,
      licenses: [{ license: { name: 'BSD-3-Clause and others: runtime/licenses/CHROMIUM-CREDITS.html' } }],
    },
    ...(pico ? [{ type: 'application', name: 'pico (the compiler that built the backend)', version: pico }] : []),
    ...dependencies.map(d => ({ type: 'library', name: d.name, version: d.version, licenses: license(d.license) })),
  ]

  return {
    bomFormat: 'CycloneDX',
    specVersion: '1.5',
    version: 1,
    metadata: { component: { type: 'application', name: config.app.name, version: config.app.version } },
    components,
  }
}

/**
 * An installed package's name, version and license: its node_modules/<name>/package.json, looked
 * up from `dir` the way Node does (its `exports` may not include ./package.json).
 *
 * @param dir where to start
 * @param name the package
 * @returns it, or null if it isn't installed
 */
function installed(dir: string, name: string): Dependency | null {
  const manifest = join(dir, 'node_modules', name, 'package.json')
  if (existsSync(manifest)) {
    const pkg = JSON.parse(readFileSync(manifest, 'utf8')) as { version?: string, license?: string }

    return { name, version: pkg.version ?? 'unknown', license: pkg.license }
  }

  return dirname(dir) === dir ? null : installed(dirname(dir), name)
}

/**
 * The frontend's runtime dependencies (its package.json `dependencies`, with what's installed).
 *
 * @param frontend the frontend's root
 * @returns them, sorted by name
 */
export function frontendDependencies(frontend: string): Dependency[] {
  const manifest = join(frontend, 'package.json')
  if (!existsSync(manifest)) {
    return []
  }
  const { dependencies = {} } = JSON.parse(readFileSync(manifest, 'utf8')) as { dependencies?: Record<string, string> }

  return Object.keys(dependencies).sort().map(name => installed(frontend, name)).filter((d): d is Dependency => d !== null)
}
