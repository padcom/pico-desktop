// A stand-in for the Pico backend, in TypeScript over an in-memory link: the UI then runs in any
// browser (`pnpm ui`) and in Vitest. It speaks the same protocol as the host, so the app can't
// tell the difference; keep it in step with packages/contracts/app.cddl.
import { type Transport, memoryTransportPair } from '@pico-desktop/communicator'
import { type Envelope, decodeEnvelope, encodeEnvelope } from '@pico-desktop/protocol'

type Outgoing = Omit<Envelope, 'v'>
type Answer = (e: Envelope, send: (e: Outgoing) => void) => void

const clocks = new Map<number, ReturnType<typeof setInterval>>()

const answers: Record<string, Answer> = {
  'greeting.hello': (e, send) => {
    const { name } = e.p as { name: string }
    send({ k: 'response', id: e.id!, p: { message: `Hello, ${name}! (from the mock backend)` } })
  },
  'clock.ticks': (e, send) => {
    const s = e.id!
    const { intervalMs = 1000 } = (e.p ?? {}) as { intervalMs?: number }
    let n = 0
    send({ k: 'response', id: e.id!, s })
    clocks.set(s, setInterval(() => send({ k: 'event', n: 'clock.ticks', s, p: { n: ++n } }), intervalMs))
  },
}

function receive(e: Envelope, send: (e: Outgoing) => void) {
  if (e.k === 'lifecycle' && e.n === 'hello') {
    send({ k: 'lifecycle', n: 'ready', e: 1 })
  } else if (e.k === 'unsubscribe') {
    clearInterval(clocks.get(e.s!))
  } else if (e.k === 'request' || e.k === 'subscribe') {
    const answer = answers[e.n!]
    if (answer) {
      answer(e, send)
    } else {
      send({ k: 'error', id: e.id!, err: { code: 'UNKNOWN_METHOD', message: `the mock has no ${e.n}` } })
    }
  }
}

/** The app's end of a link to the mock backend. */
export function mockBackend(): Transport {
  const [app, backend] = memoryTransportPair()
  const send = (e: Outgoing) => backend.send(encodeEnvelope(e))
  backend.connect(bytes => receive(decodeEnvelope(bytes), send))

  return app
}
