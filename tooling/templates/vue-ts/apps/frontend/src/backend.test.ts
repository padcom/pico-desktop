import { memoryTransportPair } from '@pico-desktop/communicator'
import { decodeEnvelope, encodeEnvelope } from '@pico-desktop/protocol'
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

  it('stops the clock when its scope ends, even if the subscription resolves later', async () => {
    const seen: string[] = []
    const [app, backend] = memoryTransportPair()
    backend.connect(bytes => {
      const e = decodeEnvelope(bytes)
      seen.push(e.k)
      if (e.k === 'lifecycle' && e.n === 'hello') {
        backend.send(encodeEnvelope({ k: 'lifecycle', n: 'ready', e: 1 }))
      } else if (e.k === 'subscribe') {
        // Answer after the scope is gone.
        setTimeout(() => backend.send(encodeEnvelope({ k: 'response', id: e.id!, s: e.id! })), 20)
      }
    })
    const scope = effectScope()
    scope.run(() => useBackend(app))
    await sleep(5)
    scope.stop()
    await sleep(60)
    expect(seen).toContain('unsubscribe')
  })

  it('unsubscribes on scope end when already subscribed', async () => {
    const seen: string[] = []
    const [app, backend] = memoryTransportPair()
    backend.connect(bytes => {
      const e = decodeEnvelope(bytes)
      seen.push(e.k)
      if (e.k === 'lifecycle' && e.n === 'hello') {
        backend.send(encodeEnvelope({ k: 'lifecycle', n: 'ready', e: 1 }))
      } else if (e.k === 'subscribe') {
        backend.send(encodeEnvelope({ k: 'response', id: e.id!, s: e.id! }))
      }
    })
    const scope = effectScope()
    scope.run(() => useBackend(app))
    await sleep(30)
    scope.stop()
    await sleep(10)
    expect(seen).toContain('unsubscribe')
  })
})
