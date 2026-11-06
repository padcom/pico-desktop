import { describe, expect, it } from 'vitest'
import { App } from './app'
import { mockBackend } from './mock-backend'

const sleep = (ms: number) => new Promise(resolve => {
  setTimeout(resolve, ms)
})

describe('the app', () => {
  it('greets through the backend', async () => {
    const messages: string[] = []
    const app = new App(mockBackend(), { state: () => undefined, message: m => messages.push(m), tick: () => undefined })
    await app.greet('Pico')
    expect(messages).toEqual(['Hello, Pico! (from the mock backend)'])
  })

  it('reports the connection becoming ready', async () => {
    const states: string[] = []
    new App(mockBackend(), { state: s => states.push(s), message: () => undefined, tick: () => undefined })
    await sleep(10)
    expect(states).toContain('ready')
  })
})
