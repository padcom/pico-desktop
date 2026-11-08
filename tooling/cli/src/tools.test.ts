import { describe, expect, it } from 'vitest'
import { isBackendSource, worthShowing } from './dev'
import { compareVersions, frameworkHosts, picoVersionIn } from './tools'

describe('compareVersions', () => {
  it('orders x.y.z versions numerically', () => {
    expect(compareVersions('0.4.1', '0.4.2')).toBeLessThan(0)
    expect(compareVersions('0.10.0', '0.4.2')).toBeGreaterThan(0)
    expect(compareVersions('1.0.0', '1.0.0')).toBe(0)
  })
})

describe('frameworkHosts', () => {
  it('knows where each system\'s build puts the host', () => {
    expect(frameworkHosts('linux')).toEqual([expect.stringMatching(/cef-host\/pico-desktop-host$/u)])
    expect(frameworkHosts('darwin')).toEqual([
      expect.stringMatching(/cef-host\/pico-desktop-host\.app\/Contents\/MacOS\/pico-desktop-host$/u),
    ])
    expect(frameworkHosts('win32')).toEqual([
      expect.stringMatching(/cef-host\/Release\/pico-desktop-host\.exe$/u),
      expect.stringMatching(/cef-host\/pico-desktop-host\.exe$/u),
    ])
  })
})

describe('isBackendSource', () => {
  it('is Pico sources and manifests, not tests', () => {
    expect(isBackendSource('app.pi')).toBe(true)
    expect(isBackendSource('generated/app.pi')).toBe(true)
    expect(isBackendSource('project.json')).toBe(true)
    expect(isBackendSource('app.test.pi')).toBe(false)
    expect(isBackendSource('notes.md')).toBe(false)
  })
})

describe('worthShowing', () => {
  it('hides Chromium\'s vsync noise, keeps the rest', () => {
    const vsync = '[1:1:1107/000211.895681:ERROR:ui/gl/gl_surface_presentation_helper.cc:260] '
      + 'GetVSyncParametersIfAvailable() failed for 1 times!'
    expect(worthShowing(vsync)).toBe(false)
    expect(worthShowing('pd-host: the backend is ready (epoch 1)')).toBe(true)
  })
})

describe('picoVersionIn', () => {
  it('reads the Pico compiler\'s version, not nano\'s', () => {
    expect(picoVersionIn('pico 0.4.1\n')).toBe('0.4.1')
    expect(picoVersionIn(' GNU nano, version 8.4.1\n')).toBeUndefined()
  })
})
