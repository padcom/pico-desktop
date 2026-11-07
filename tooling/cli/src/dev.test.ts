import { describe, expect, it } from 'vitest'
import { resolveConfig } from './config'
import { devHostArgs } from './dev'

describe('devHostArgs', () => {
  const config = resolveConfig('/app', { app: { id: 'com.example.x' }, capabilities: ['window'] })

  it('runs the dev backend against the dev server, controlled over stdin', () => {
    const args = devHostArgs(config, 'http://127.0.0.1:5173', {})
    expect(args.slice(0, 5)).toEqual([
      '--dev',
      '--dev-url=http://127.0.0.1:5173',
      '--app-id=com.example.x',
      '--backend=/app/.pico-desktop/dev/backend',
      '--control=stdin',
    ])
    expect(args).toContain('--capabilities=window')
    expect(args.filter(a => a.startsWith('--liveness') || a === '--trace-ipc')).toEqual([])
  })

  it('passes the app\'s liveness timings, and only when they aren\'t the default', () => {
    const custom = resolveConfig('/app', { app: { id: 'x' }, liveness: { intervalMs: 1000, timeoutMs: 4000 } })
    expect(devHostArgs(custom, 'http://127.0.0.1:5173', {})).toContain('--liveness=1000/4000')
    expect(devHostArgs(config, 'http://127.0.0.1:5173', {}).some(a => a.startsWith('--liveness'))).toBe(false)
    // Debugging wins: the host takes the last --liveness.
    expect(devHostArgs(custom, 'http://127.0.0.1:5173', { debug: true }).at(-1)).toBe('--liveness=off')
  })

  it('turns liveness pings off for a debugger, and traces when asked', () => {
    const args = devHostArgs(config, 'http://127.0.0.1:5173', { debug: true, traceIpc: true })
    expect(args).toContain('--liveness=off')
    expect(args).toContain('--trace-ipc')
  })
})
