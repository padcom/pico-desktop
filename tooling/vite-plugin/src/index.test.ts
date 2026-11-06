import { describe, it, expect } from 'vitest'
import { picoDesktop } from '.'

describe('vite plugin', () => {
  it('has a name', () => {
    expect(picoDesktop().name).toBe('pico-desktop')
  })
})
