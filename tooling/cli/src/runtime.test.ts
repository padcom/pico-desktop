import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { appendFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterAll, describe, expect, it } from 'vitest'
import { createServer } from 'node:http'
import { RuntimeError, fetchRuntime, unpackRuntime, verifyRuntime } from './runtime'

const scratch = mkdtempSync(join(tmpdir(), 'pd-runtime-'))

afterAll(() => rmSync(scratch, { recursive: true, force: true }))

const sha256 = (data: string | Buffer) => createHash('sha256').update(data).digest('hex')

/**
 * A tiny runtime: two files and a manifest of their hashes.
 *
 * @param name its directory under the scratch directory
 * @param target the target its manifest names
 * @returns its directory
 */
function fakeRuntime(name: string, target = 'linux-arm64'): string {
  const dir = join(scratch, name)
  mkdirSync(join(dir, 'locales'), { recursive: true })
  writeFileSync(join(dir, 'pico-desktop-host'), 'host')
  writeFileSync(join(dir, 'locales', 'en-US.pak'), 'pak')
  const manifest = {
    name: 'pico-desktop-runtime', version: '0.0.0', target, host: 'pico-desktop-host',
    cef: { version: 'x', chromium: 'y' }, protocol: 1, pico: { minimum: '0.4.2' },
    files: { 'locales/en-US.pak': sha256('pak'), 'pico-desktop-host': sha256('host') },
  }
  writeFileSync(join(dir, 'runtime.json'), JSON.stringify(manifest))

  return dir
}

describe('verifyRuntime', () => {
  it('accepts a runtime whose files match its manifest', () => {
    expect(verifyRuntime(fakeRuntime('ok'), 'linux-arm64').version).toBe('0.0.0')
  })

  it('refuses a changed file, naming it', () => {
    const dir = fakeRuntime('changed')
    appendFileSync(join(dir, 'pico-desktop-host'), 'tampered')
    expect(() => verifyRuntime(dir, 'linux-arm64')).toThrow(/pico-desktop-host doesn't match runtime\.json/u)
  })

  it('refuses a missing file and a file it doesn\'t list', () => {
    const missing = fakeRuntime('missing')
    rmSync(join(missing, 'locales', 'en-US.pak'))
    expect(() => verifyRuntime(missing, 'linux-arm64')).toThrow(RuntimeError)
    const extra = fakeRuntime('extra')
    writeFileSync(join(extra, 'libinjected.so'), 'x')
    expect(() => verifyRuntime(extra, 'linux-arm64')).toThrow(/doesn't list: libinjected\.so/u)
  })

  it('refuses another target\'s runtime, and a directory that isn\'t one', () => {
    expect(() => verifyRuntime(fakeRuntime('x64', 'linux-x64'), 'linux-arm64')).toThrow(/for linux-x64, not linux-arm64/u)
    expect(() => verifyRuntime(scratch, 'linux-arm64')).toThrow(/no runtime\.json/u)
  })
})

describe('unpackRuntime', () => {
  const archive = (name: string) => {
    fakeRuntime(name)
    const file = join(scratch, `${name}.tar.gz`)
    execFileSync('tar', ['-C', scratch, '-czf', file, name])

    return file
  }

  it('unpacks an archive that matches its .sha256 into the cache, once', async () => {
    const file = archive('packed')
    writeFileSync(`${file}.sha256`, `${sha256(readFileSync(file))}  packed.tar.gz\n`)
    const cache = join(scratch, 'cache')
    const dir = await unpackRuntime(file, cache)
    expect(verifyRuntime(dir, 'linux-arm64').target).toBe('linux-arm64')
    rmSync(file) // offline reuse: the cached copy is enough
    expect(await unpackRuntime(file, cache)).toBe(dir)
  })

  it('refuses an archive that doesn\'t match its .sha256, or has none', async () => {
    const file = archive('forged')
    writeFileSync(`${file}.sha256`, `${sha256('something else')}  forged.tar.gz\n`)
    await expect(unpackRuntime(file, join(scratch, 'cache2'))).rejects.toThrow(/doesn't match its SHA-256/u)
    expect(existsSync(join(scratch, 'cache2', 'forged'))).toBe(false)
    rmSync(`${file}.sha256`)
    await expect(unpackRuntime(file, join(scratch, 'cache2'))).rejects.toThrow(/no forged\.tar\.gz\.sha256/u)
  })
})

/**
 * Serves `files` on 127.0.0.1 (anything else is a 404).
 *
 * @param files path → body
 * @returns the server and its port
 */
async function serve(files: Record<string, Buffer>) {
  const server = createServer((req, res) => {
    const body = files[req.url ?? '']
    res.writeHead(body ? 200 : 404)
    res.end(body)
  })
  await new Promise<void>(resolve => {
    server.listen(0, '127.0.0.1', resolve)
  })

  return { server, port: (server.address() as { port: number }).port }
}

describe('fetchRuntime', () => {
  it('downloads an archive and its .sha256, checks and unpacks it, and works offline afterwards', async () => {
    fakeRuntime('served')
    const archive = join(scratch, 'served.tar.gz')
    execFileSync('tar', ['-C', scratch, '-czf', archive, 'served'])
    const files: Record<string, Buffer> = {
      '/r/served.tar.gz': readFileSync(archive),
      '/r/served.tar.gz.sha256': Buffer.from(`${sha256(readFileSync(archive))}  served.tar.gz\n`),
    }
    const { server, port } = await serve(files)
    const cache = join(scratch, 'downloaded')
    const dir = await fetchRuntime(`http://127.0.0.1:${port}/r/served.tar.gz`, cache)
    expect(verifyRuntime(dir, 'linux-arm64').version).toBe('0.0.0')
    await expect(fetchRuntime(`http://127.0.0.1:${port}/r/missing.tar.gz`, cache)).rejects.toThrow(/HTTP 404/u)
    server.close()
    expect(await fetchRuntime(`http://127.0.0.1:${port}/r/served.tar.gz`, cache)).toBe(dir) // offline
  })

  it('refuses a runtime that speaks another protocol', () => {
    const dir = fakeRuntime('future')
    const manifest = JSON.parse(readFileSync(join(dir, 'runtime.json'), 'utf8')) as Record<string, unknown>
    writeFileSync(join(dir, 'runtime.json'), JSON.stringify({ ...manifest, protocol: 2 }))
    expect(() => verifyRuntime(dir, 'linux-arm64')).toThrow(/speaks protocol 2/u)
  })
})
