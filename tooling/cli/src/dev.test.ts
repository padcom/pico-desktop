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

  it('turns liveness pings off for a debugger, and traces when asked', () => {
    const args = devHostArgs(config, 'http://127.0.0.1:5173', { debug: true, traceIpc: true })
    expect(args).toContain('--liveness=off')
    expect(args).toContain('--trace-ipc')
  })
})
