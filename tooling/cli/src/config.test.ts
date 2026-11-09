import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { ConfigError, resolveConfig } from './config'

// The table `isValidAppId` (packages/communicator-pico/app_dirs.pi) is tested with too: the two must agree.
const APP_IDS = fileURLToPath(new URL('../../../packages/communicator-pico/app-ids.txt', import.meta.url))

/**
 * Whether the configuration accepts `id` as the app's ID.
 *
 * @param id the ID
 * @returns true if it does
 */
function accepts(id: string): boolean {
  try {
    resolveConfig('/app', { app: { id } })

    return true
  } catch (error) {
    expect(error).toBeInstanceOf(ConfigError)

    return false
  }
}

describe('app.id', () => {
  it('accepts and refuses the IDs of the table shared with the Pico backend library', () => {
    const lines = readFileSync(APP_IDS, 'utf8').split('\n').filter(line => line !== '' && !line.startsWith('#'))
    expect(lines.length).toBeGreaterThan(10)

    for (const line of lines) {
      const [verdict, ...rest] = line.split('|')
      const id = rest.join('|')

      expect(`${id} is ${accepts(id) ? 'valid' : 'invalid'}`).toBe(`${id} is ${verdict}`)
    }
  })
})

describe('resolveConfig', () => {
  it('fills in the standard layout', () => {
    const config = resolveConfig('/app', { app: { id: 'com.example.demo' } })
    expect(config).toEqual({
      root: '/app',
      app: { id: 'com.example.demo', name: 'com.example.demo', version: '0.0.0' },
      capabilities: [],
      window: { width: 1024, height: 768 },
      closeGraceMs: 3000,
      liveness: { intervalMs: 5000, timeoutMs: 15000 },
      icon: undefined,
      frontend: { root: '/app/apps/frontend' },
      backend: { root: '/app/apps/backend', entry: 'main.pi' },
      contracts: [{
        source: '/app/packages/contracts/app.cddl',
        typescript: '/app/apps/frontend/src/generated/app.ts',
        pico: '/app/apps/backend/generated/app.pi',
      }],
      host: undefined,
      runtime: undefined,
    })
  })

  it('makes given paths absolute', () => {
    const config = resolveConfig('/app', { app: { id: 'demo' }, backend: { root: 'pico' }, host: 'bin/host' })
    expect(config.backend.root).toBe('/app/pico')
    expect(config.host).toBe('/app/bin/host')
  })
})

describe('resolveConfig checks', () => {
  it('the liveness timings, as the host does', () => {
    expect(resolveConfig('/app', { app: { id: 'demo' }, liveness: false }).liveness).toBe(false)
    const app = { id: 'demo' }
    const timed = (i: number, t: number) => () => resolveConfig('/app', { app, liveness: { intervalMs: i, timeoutMs: t } })
    expect(timed(50, 1000)).toThrow(ConfigError)
    expect(timed(1000, 1000)).toThrow(ConfigError)
  })

  it('the capabilities', () => {
    expect(resolveConfig('/app', { app: { id: 'demo' }, capabilities: ['window', 'window'] }).capabilities).toEqual(['window'])
    // @ts-expect-error -- an unknown capability on purpose
    expect(() => resolveConfig('/app', { app: { id: 'demo' }, capabilities: ['camera'] })).toThrow(ConfigError)
  })

  it('the app ID, like the host does', () => {
    expect(() => resolveConfig('/app', { app: { id: 'Not Valid' } })).toThrow(ConfigError)
    expect(() => resolveConfig('/app', { app: { id: 'a..b' } })).toThrow(ConfigError)
  })
})
