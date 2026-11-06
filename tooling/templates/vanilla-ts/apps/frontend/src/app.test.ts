import { memoryTransportPair } from '@pico-desktop/communicator'
import { type Envelope, decodeEnvelope, encodeEnvelope } from '@pico-desktop/protocol'
import { describe, expect, it } from 'vitest'
import { App } from './app'

// The host and a scripted backend on the other end of the app's link.
function fakeBackend() {
  const [app, backend] = memoryTransportPair()
  const send = (e: Omit<Envelope, 'v'>) => backend.send(encodeEnvelope(e))
  backend.connect(bytes => {
    const e = decodeEnvelope(bytes)
    if (e.k === 'lifecycle') {
      send({ k: 'lifecycle', n: 'ready', e: 1 })
    } else if (e.n === 'greeting.hello') {
      send({ k: 'response', id: e.id!, p: { message: `Hello, ${(e.p as { name: string }).name}!` } })
    } else if (e.n === 'clock.ticks') {
      send({ k: 'response', id: e.id!, s: 1 })
    }
  })

  return app
}

describe('the app', () => {
  it('greets through the backend', async () => {
    const messages: string[] = []
    const app = new App(fakeBackend(), { state: () => undefined, message: m => messages.push(m), tick: () => undefined })
    await app.greet('Pico')
    expect(messages).toEqual(['Hello, Pico!'])
  })
})
