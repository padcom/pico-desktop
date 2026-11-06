import { describe, expect, it } from 'vitest'
import { launcher, slug } from './build'
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
    expect(script).toContain('--backend="$here/backend/backend" --capabilities=window "$@"')
    expect(launcher(resolveConfig('/app', { app: { id: 'x' } }))).not.toContain('--capabilities')
  })
})
