import { mkdirSync, mkdtempSync, rmSync, symlinkSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterAll, describe, expect, it } from 'vitest'
import { notices, productionPackages } from './notices'

const root = mkdtempSync(join(tmpdir(), 'notices-'))
afterAll(() => rmSync(root, { recursive: true, force: true }))

function pkg(dir: string, manifest: object, license?: string) {
  mkdirSync(dir, { recursive: true })
  writeFileSync(join(dir, 'package.json'), JSON.stringify(manifest))

  if (license) {
    writeFileSync(join(dir, 'LICENSE'), license)
  }
}

describe('third-party notices', () => {
  it('collect production dependencies transitively, through pnpm-style links, once each', () => {
    const app = join(root, 'app')
    pkg(app, { dependencies: { a: '1' } })
    // a lives in a store and is linked into node_modules; it depends on b, which has no license file.
    const store = join(root, 'store')
    pkg(join(store, 'a'), { version: '1.0.0', license: 'MIT', dependencies: { b: '2' } }, 'MIT License\nCopyright A')
    pkg(join(store, 'a', 'node_modules', 'b'), { version: '2.0.0', license: 'ISC' })
    mkdirSync(join(app, 'node_modules'), { recursive: true })
    symlinkSync(join(store, 'a'), join(app, 'node_modules', 'a'))
    // A devDependency isn't shipped.
    pkg(join(app, 'node_modules', 'dev'), { version: '9.0.0' }, 'not shipped')
    const packages = productionPackages(app)
    expect(packages.map(p => `${p.name}@${p.version} ${p.license}`)).toEqual(['a@1.0.0 MIT', 'b@2.0.0 ISC'])
    const text = notices('my-app', packages)
    expect(text).toContain('a 1.0.0 (MIT)')
    expect(text).toContain('Copyright A')
    expect(text).toContain(`b 2.0.0 (ISC)\n${'-'.repeat(78)}\n\n(The package has no license file`)
    expect(text).not.toContain('not shipped')
  })
})
