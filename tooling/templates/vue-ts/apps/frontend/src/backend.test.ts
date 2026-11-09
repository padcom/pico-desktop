import { type Transport, memoryTransportPair } from '@pico-desktop/communicator'
import { decodeEnvelope, encodeEnvelope } from '@pico-desktop/protocol'
import { describe, expect, it } from 'vitest'
import { effectScope } from 'vue'
import { useBackend } from './backend'
import { mockBackend } from './mock-backend'

const sleep = (ms: number) => new Promise(resolve => {
  setTimeout(resolve, ms)
})

/**
 * A backend that records what it receives and answers subscriptions after `delay` ms.
 *
 * @param delay how long a subscribe takes to be answered
 * @returns the app's end of the link and the kinds of the envelopes received
 */
function recordingBackend(delay: number): { app: Transport, seen: string[] } {
  const seen: string[] = []
  const [app, backend] = memoryTransportPair()
  backend.connect(bytes => {
    const e = decodeEnvelope(bytes)
    seen.push(e.k)

    if (e.k === 'lifecycle' && e.n === 'hello') {
      backend.send(encodeEnvelope({ k: 'lifecycle', n: 'ready', e: 1 }))
    } else if (e.k === 'subscribe') {
      setTimeout(() => backend.send(encodeEnvelope({ k: 'response', id: e.id!, s: e.id! })), delay)
    }
  })

  return { app, seen }
}

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

describe('useBackend\'s clock', () => {
  it('stops when its scope ends, even if the subscription resolves later', async () => {
    const { app, seen } = recordingBackend(20)
    const scope = effectScope()
    scope.run(() => useBackend(app))
    await sleep(5)
    scope.stop()
    await sleep(60)

    expect(seen).toContain('unsubscribe')
  })

  it('stops when its scope ends after it was subscribed', async () => {
    const { app, seen } = recordingBackend(0)
    const scope = effectScope()
    scope.run(() => useBackend(app))
    await sleep(30)
    scope.stop()
    await sleep(10)

    expect(seen).toContain('unsubscribe')
  })
})
