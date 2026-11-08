// The built frontend must satisfy the production CSP (`script-src 'self'; style-src 'self'`):
// no inline script or style, CSS from a linked file, and no eval or `new Function` in the bundle.
import { existsSync, readFileSync, readdirSync } from 'node:fs'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { build } from 'vite'
import { beforeAll, describe, expect, it } from 'vitest'

const root = fileURLToPath(new URL('..', import.meta.url))
const out = join(root, 'dist-csp-test')

describe('the production build', () => {
  let html = ''
  let js = ''

  beforeAll(async () => {
    await build({ root, logLevel: 'silent', build: { outDir: out, emptyOutDir: true } })
    html = readFileSync(join(out, 'index.html'), 'utf8')
    const assets = join(out, 'assets')
    js = readdirSync(assets).filter(f => f.endsWith('.js')).map(f => readFileSync(join(assets, f), 'utf8')).join('\n')
  }, 120_000)

  it('has no inline script or style', () => {
    expect(html).not.toMatch(/<script(?![^>]*\ssrc=)[^>]*>/iu)
    expect(html).not.toMatch(/<style[\s>]/iu)
    expect(html).not.toMatch(/\sstyle=/iu)
    expect(html).not.toMatch(/\son[a-z]+=/iu)
  })

  it('links its CSS from a file', () => {
    expect(html).toMatch(/<link[^>]+rel="stylesheet"[^>]+href="\.\/assets\/[^"]+\.css"/iu)
    expect(existsSync(join(out, 'assets'))).toBe(true)
  })

  it('has no eval or new Function', () => {
    expect(js).not.toMatch(/\beval\s*\(/u)
    expect(js).not.toMatch(/new Function\s*\(/u)
  })
})
