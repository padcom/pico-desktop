import { execFileSync } from 'node:child_process'
import { chmodSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { describe, expect, it } from 'vitest'
import { launcher, releaseBuildArgs, slug } from './build'
import { resolveConfig } from './config'

describe('the bundle', () => {
  const app = { id: 'com.example.my-app', name: 'My App!', version: '1.2.3' }
  const config = resolveConfig('/app', { app, capabilities: ['window'] })

  it('is named after the app', () => {
    expect(slug(config)).toBe('my-app')
    expect(slug(resolveConfig('/app', { app: { id: 'x' } }))).toBe('x')
  })

  it('starts the host with the app, its ID and its capabilities', () => {
    const script = launcher(config)
    expect(script.startsWith('#!/bin/sh\n')).toBe(true)
    expect(script).toContain('exec "$here/runtime/pico-desktop-host" --app-dir="$here/app" --app-id=\'com.example.my-app\'')
    expect(script).toContain(`--backend="$here/backend/backend" '--window-size=1024x768' '--close-grace=3000' `
      + `'--capabilities=window' "$@"`)
    expect(launcher(resolveConfig('/app', { app: { id: 'x' } }))).not.toContain('--capabilities')
  })

  it('quotes the window title for the shell', () => {
    const titled = resolveConfig('/app', { app: { id: 'x' }, window: { title: "Bob's app", width: 640 } })
    expect(launcher(titled)).toContain(`'--window-size=640x768' '--close-grace=3000' '--window-title=Bob'\\''s app'`)
  })
})

describe('the launcher, run by sh', () => {
  it('passes the host exactly its arguments, whatever the title holds', () => {
    const dir = mkdtempSync(join(tmpdir(), 'pd-launcher-'))
    mkdirSync(join(dir, 'runtime'))
    writeFileSync(join(dir, 'runtime', 'pico-desktop-host'), '#!/bin/sh\nfor a in "$@"; do echo "$a"; done\n')
    chmodSync(join(dir, 'runtime', 'pico-desktop-host'), 0o755)
    const config = resolveConfig('/app', { app: { id: 'x' }, window: { title: "Bob's $HOME `app`" }, capabilities: ['window'] })
    writeFileSync(join(dir, 'app'), launcher(config))
    chmodSync(join(dir, 'app'), 0o755)
    const args = execFileSync(join(dir, 'app'), ['--extra'], { encoding: 'utf8' }).trim().split('\n')
    rmSync(dir, { recursive: true, force: true })
    expect(args).toEqual([
      `--app-dir=${dir}/app`,
      '--app-id=x',
      `--backend=${dir}/backend/backend`,
      '--window-size=1024x768',
      '--close-grace=3000',
      '--capabilities=window',
      "--window-title=Bob's $HOME `app`",
      '--extra',
    ])
  })
})

describe('releaseBuildArgs', () => {
  it('names the target, so the backend is built for the architecture\'s baseline CPU, not this machine\'s', () => {
    expect(releaseBuildArgs('/app/main.pi', '/out/backend', 'linux-x64'))
      .toEqual(['build', '--release', '--target', 'linux-x64', '/app/main.pi', '-o', '/out/backend'])
  })
})
