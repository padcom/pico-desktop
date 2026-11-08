// @vitest-environment node
// The built frontend must satisfy the production CSP (`script-src 'self'; style-src 'self'`):
// no inline script or style, CSS from a linked file, and no eval or `new Function` in the bundle.
// (The node environment: this test reads the build from disk, and jsdom's `import.meta.url` isn't a file URL.)
import { existsSync, readFileSync, readdirSync } from 'node:fs'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { build } from 'vite'
import { beforeAll, describe, expect, it } from 'vitest'

const root = fileURLToPath(new URL('..', import.meta.url))
const out = join(root, 'dist-csp-test')

/**
 * Builds the frontend like `pnpm build` does.
 *
 * @returns the built index.html and the JavaScript of all its chunks
 */
async function buildFrontend() {
  await build({ root, logLevel: 'silent', build: { outDir: out, emptyOutDir: true } })
  const assets = join(out, 'assets')
  const chunks = readdirSync(assets).filter(f => f.endsWith('.js'))

  return {
    html: readFileSync(join(out, 'index.html'), 'utf8'),
    js: chunks.map(f => readFileSync(join(assets, f), 'utf8')).join('\n'),
  }
}

describe('the production build', () => {
  let built = { html: '', js: '' }

  beforeAll(async () => {
    built = await buildFrontend()
  }, 120_000)

  it('has no inline script or style', () => {
    expect(built.html).not.toMatch(/<script(?![^>]*\ssrc=)[^>]*>/iu)
    expect(built.html).not.toMatch(/<style[\s>]/iu)
    expect(built.html).not.toMatch(/\sstyle=/iu)
    expect(built.html).not.toMatch(/\son[a-z]+=/iu)
  })

  it('links its CSS from a file', () => {
    expect(built.html).toMatch(/<link[^>]+rel="stylesheet"[^>]+href="\.\/assets\/[^"]+\.css"/iu)
    expect(existsSync(join(out, 'assets'))).toBe(true)
  })

  it('hoists no inline style attribute into the JavaScript', () => {
    // Static markup with style="…" compiles to an HTML string that is parsed at run time, which
    // `style-src 'self'` blocks. (Bind a class instead.)
    expect(built.js).not.toMatch(/\sstyle\s*=\s*\\?["']/u)
  })

  it('has no eval or new Function', () => {
    expect(built.js).not.toMatch(/\beval\s*\(/u)
    expect(built.js).not.toMatch(/new Function\s*\(/u)
  })
})
