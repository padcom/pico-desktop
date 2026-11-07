import { execFileSync } from 'node:child_process'
import { chmodSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readlinkSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterAll, describe, expect, it } from 'vitest'
import { appImagePath, desktopEntry, makeAppDir } from './appdir'
import { launcher } from './build'
import { resolveConfig } from './config'

const scratch = mkdtempSync(join(tmpdir(), 'pd-appdir-'))

afterAll(() => rmSync(scratch, { recursive: true, force: true }))

describe('the AppDir', () => {
  const config = resolveConfig('/app', { app: { id: 'com.example.demo', name: 'Demo App', version: '1.0.0' } })

  it('has the bundle, AppRun, a .desktop entry and an icon', () => {
    const bundle = join(scratch, 'demo-1.0.0-linux-arm64')
    mkdirSync(join(bundle, 'runtime'), { recursive: true })
    writeFileSync(join(bundle, 'runtime', 'pico-desktop-host'), '#!/bin/sh\necho "host $*"\n')
    chmodSync(join(bundle, 'runtime', 'pico-desktop-host'), 0o755)
    writeFileSync(join(bundle, 'demo'), launcher(config))
    const dir = makeAppDir(config, { dir: bundle, name: 'demo' })
    expect(existsSync(join(dir, 'runtime', 'pico-desktop-host'))).toBe(true)
    expect(readFileSync(join(dir, 'demo.desktop'), 'utf8')).toContain('Icon=demo\n')
    expect(readFileSync(join(dir, 'demo.svg'), 'utf8')).toContain('>D</text>')
    expect(readlinkSync(join(dir, '.DirIcon'))).toBe('demo.svg')
    // AppRun starts the host from wherever the AppDir is (mounted or extracted).
    expect(execFileSync(join(dir, 'AppRun'), ['--x'], { encoding: 'utf8' })).toContain(`--app-dir=${dir}/app`)
  })

  it('names the entry and the AppImage', () => {
    expect(desktopEntry(config, 'demo')).toContain('Name=Demo App\nExec=AppRun\n')
    expect(appImagePath('/d/demo-1.0.0-linux-arm64')).toBe('/d/demo-1.0.0-aarch64.AppImage')
    expect(appImagePath('/d/demo-1.0.0-linux-x64')).toBe('/d/demo-1.0.0-x86_64.AppImage')
  })
})
