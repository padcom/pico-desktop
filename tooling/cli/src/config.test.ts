import { describe, expect, it } from 'vitest'
import { ConfigError, resolveConfig } from './config'

describe('resolveConfig', () => {
  it('fills in the standard layout', () => {
    const config = resolveConfig('/app', { app: { id: 'com.example.demo' } })
    expect(config).toEqual({
      root: '/app',
      app: { id: 'com.example.demo', name: 'com.example.demo', version: '0.0.0' },
      frontend: { root: '/app/apps/frontend' },
      backend: { root: '/app/apps/backend', entry: 'main.pi' },
      contracts: [{
        source: '/app/packages/contracts/app.cddl',
        typescript: '/app/apps/frontend/src/generated/app.ts',
        pico: '/app/apps/backend/generated/app.pi',
      }],
      host: undefined,
    })
  })

  it('makes given paths absolute', () => {
    const config = resolveConfig('/app', { app: { id: 'demo' }, backend: { root: 'pico' }, host: 'bin/host' })
    expect(config.backend.root).toBe('/app/pico')
    expect(config.host).toBe('/app/bin/host')
  })

  it('checks the app ID like the host does', () => {
    expect(() => resolveConfig('/app', { app: { id: 'Not Valid' } })).toThrow(ConfigError)
    expect(() => resolveConfig('/app', { app: { id: 'a..b' } })).toThrow(ConfigError)
  })
})
