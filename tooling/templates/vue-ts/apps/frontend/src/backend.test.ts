import { effectScope } from 'vue'
import { describe, expect, it } from 'vitest'
import { useBackend } from './backend'
import { mockBackend } from './mock-backend'

const sleep = (ms: number) => new Promise(resolve => {
  setTimeout(resolve, ms)
})

describe('useBackend', () => {
  it('greets and reports the connection', async () => {
    const scope = effectScope()
    const backend = scope.run(() => useBackend(mockBackend()))!
    await backend.greet('Pico')
    expect(backend.message.value).toBe('Hello, Pico! (from the mock backend)')
    await sleep(10)
    expect(backend.state.value).toBe('ready')
    scope.stop()
  })
})
