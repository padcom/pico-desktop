import { describe, expect, it } from 'vitest'
import { resolveConfig } from './config'
import type { RuntimeManifest } from './runtime'
import { sbom } from './sbom'

const runtime: RuntimeManifest = {
  name: 'pico-desktop-runtime', version: '0.1.0', target: 'linux-arm64', host: 'pico-desktop-host',
  cef: { version: '156.0.15', chromium: '156.0.8078.80' }, protocol: 1, pico: { minimum: '0.4.2' }, files: {},
}

describe('sbom', () => {
  it('lists the app, the runtime, CEF, Chromium, the compiler and the dependencies', () => {
    const config = resolveConfig('/app', { app: { id: 'com.example.demo', name: 'Demo', version: '1.0.0' } })
    const doc = sbom(config, runtime, '0.4.2', [{ name: 'left-pad', version: '1.3.0', license: 'WTFPL' }])
    expect(doc.bomFormat).toBe('CycloneDX')
    expect(doc.metadata.component).toEqual({ type: 'application', name: 'Demo', version: '1.0.0' })
    expect(doc.components.map(c => `${c.name}@${c.version}`)).toEqual([
      'pico-desktop-runtime@0.1.0',
      'cef@156.0.15',
      'chromium@156.0.8078.80',
      'pico (the compiler that built the backend)@0.4.2',
      'left-pad@1.3.0',
    ])
    expect(doc.components.at(-1)?.licenses).toEqual([{ license: { id: 'WTFPL' } }])
  })
})
