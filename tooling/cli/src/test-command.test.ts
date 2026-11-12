import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, describe, expect, it } from 'vitest'
import { ConfigError, resolveConfig } from './config'
import { E2eError, launchApp } from './e2e'
import { E2E_VITEST_CONFIG, e2eHostArgs, parseTestArgs, testE2e, vitestFilters } from './test-command'

describe('the arguments of test', () => {
  it('runs every test with none, and every e2e test with --e2e alone', () => {
    expect(parseTestArgs([])).toEqual({ e2e: false, files: [], name: undefined })
    expect(parseTestArgs(['--e2e'])).toEqual({ e2e: true, files: [], name: undefined })
  })

  it('takes file filters and a test name pattern after --e2e, in any order', () => {
    expect(parseTestArgs(['--e2e', 'e2e/sitl-logs.e2e.ts', 'palette'])).toEqual({
      e2e: true,
      files: ['e2e/sitl-logs.e2e.ts', 'palette'],
      name: undefined,
    })
    expect(parseTestArgs(['--e2e', '-t', 'goes on', 'logs'])).toMatchObject({ files: ['logs'], name: 'goes on' })
    expect(parseTestArgs(['logs', '--e2e', '--test-name-pattern=cancel'])).toMatchObject({ files: ['logs'], name: 'cancel' })
  })

  it('refuses filters without --e2e: the plain test runs everything', () => {
    expect(() => parseTestArgs(['logs'])).toThrow(ConfigError)
    expect(() => parseTestArgs(['-t', 'x'])).toThrow('only go with --e2e')
  })

  it('refuses an option it does not know', () => {
    expect(() => parseTestArgs(['--e2e', '--nope'])).toThrow()
  })

  it('gives Vitest the files, then -t and the pattern; none for none', () => {
    expect(vitestFilters({ files: [] })).toEqual([])
    expect(vitestFilters({ files: ['a.e2e.ts', 'b'], name: 'works' })).toEqual(['a.e2e.ts', 'b', '-t', 'works'])
    expect(vitestFilters({ files: [], name: 'works' })).toEqual(['-t', 'works'])
  })
})

describe('test --e2e', () => {
  it('runs the built app in development mode, under its own ID, with its capabilities', () => {
    const config = resolveConfig('/app', { app: { id: 'com.example.x' }, capabilities: ['dialogs'] })
    const args = e2eHostArgs(config, '/app/.pico-desktop/e2e')
    expect(args.slice(0, 4)).toEqual([
      '--dev',
      '--app-dir=/app/.pico-desktop/e2e/app',
      '--app-id=com.example.x.e2e',
      '--backend=/app/.pico-desktop/e2e/backend',
    ])
    expect(args).toContain('--capabilities=dialogs')
  })

  it('names the backend as Windows needs', () => {
    const config = resolveConfig('/app', { app: { id: 'com.example.x' } })
    expect(e2eHostArgs(config, '/out', 'win32')).toContain(`--backend=${join('/out', 'backend.exe')}`)
    expect(e2eHostArgs(config, '/out', 'linux')).toContain(`--backend=${join('/out', 'backend')}`)
  })

  it('finds only *.e2e files, one file at a time, under Node', () => {
    expect(E2E_VITEST_CONFIG).toContain('\'e2e/**/*.e2e.?(c|m)[jt]s\'')
    expect(E2E_VITEST_CONFIG).toContain('fileParallelism: false')
    expect(E2E_VITEST_CONFIG).toContain('environment: \'node\'')
  })
})

describe('test --e2e without tests or outside it', () => {
  const dirs: string[] = []
  afterEach(() => dirs.splice(0).forEach(d => rmSync(d, { recursive: true, force: true })))

  it('says where the tests go when there are none', async () => {
    const root = mkdtempSync(join(tmpdir(), 'pd-e2e-'))
    dirs.push(root)
    mkdirSync(join(root, 'apps', 'frontend', 'e2e'), { recursive: true })
    writeFileSync(join(root, 'apps', 'frontend', 'e2e', 'notes.ts'), '')
    const errors: string[] = []
    const io = { out: () => undefined, err: (e: string) => errors.push(e) }
    const code = await testE2e(resolveConfig(root, { app: { id: 'x' } }), io, { files: [] })
    expect(code).toBe(1)
    expect(errors.join('\n')).toMatch(/no end-to-end tests: put them in .*e2e, named \*\.e2e\.ts/u)
  })

  it('launchApp outside test --e2e says how to run it', async () => {
    await expect(launchApp()).rejects.toThrow(E2eError)
    await expect(launchApp()).rejects.toThrow('pico-desktop test --e2e')
  })
})
