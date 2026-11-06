import { type Transport, memoryTransportPair } from '@pico-desktop/communicator'
import { type Envelope, decodeEnvelope, encodeEnvelope } from '@pico-desktop/protocol'
import { describe, expect, it } from 'vitest'
import { HelloApp, type View } from './app'

type Outgoing = Omit<Envelope, 'v'>

/** The host and a scripted backend on the other end of the app's link. */
class FakeBackend {
  answers: Record<string, (e: Envelope) => Outgoing[]> = {}

  constructor(private readonly end: Transport) {
    end.connect(bytes => this.receive(decodeEnvelope(bytes)))
  }

  send(e: Outgoing) {
    this.end.send(encodeEnvelope(e))
  }

  private receive(e: Envelope) {
    // The host's ready for the frontend's hello; requests by name; answers to ours by `#id`.
    const ready = (): Outgoing[] => [{ k: 'lifecycle', n: 'ready', e: 1 }]
    const answer = e.k === 'lifecycle' ? ready : this.answers[e.n ?? `#${e.id}`]
    answer?.(e).forEach(r => this.send(r))
  }
}

class FakeView implements View {
  states: string[] = []
  messages: string[] = []
  counts: number[] = []
  ticks: number[] = []
  questions: string[] = []
  answer = true

  state(state: string, epoch: number) {
    this.states.push(`${state}:${epoch}`)
  }

  message(text: string) {
    this.messages.push(text)
  }

  greeted(_name: string, count: number) {
    this.counts.push(count)
  }

  tick(n: number) {
    this.ticks.push(n)
  }

  confirm(question: string) {
    this.questions.push(question)

    return Promise.resolve(this.answer)
  }
}

const settle = () => new Promise(resolve => {
  setTimeout(resolve, 10)
})

function rig() {
  const [app, backend] = memoryTransportPair()
  const fake = new FakeBackend(backend)
  fake.answers['clock.ticks'] = e => [
    { k: 'response', id: e.id!, s: 1 },
    { k: 'event', n: 'clock.ticks', s: 1, p: { n: 1 } },
  ]

  fake.answers['greeting.hello'] = e => {
    const { name } = e.p as { name: string }
    if (!name.trim()) {
      return [{ k: 'error', id: e.id!, err: { code: 'EMPTY_NAME', message: 'say who to greet' } }]
    }

    return [
      { k: 'event', n: 'greeting.greeted', p: { name, count: 1 } },
      { k: 'response', id: e.id!, p: { message: `Hello, ${name}!`, count: 1 } },
    ]
  }
  const view = new FakeView()

  return { app: new HelloApp(app, view), fake, view }
}

describe('the hello-world frontend', () => {
  it('greets through the backend and shows its event', async () => {
    const { app, view } = rig()
    await app.greet('Pico')
    await settle()
    expect(view.messages).toEqual(['Hello, Pico!'])
    expect(view.counts).toEqual([1])
    expect(view.states).toContain('ready:1')
  })

  it('shows typed errors', async () => {
    const { app, view } = rig()
    await app.greet(' ')
    expect(view.messages).toEqual(['EMPTY_NAME: say who to greet'])
  })

  it('refuses a response that breaks the contract', async () => {
    const { app, fake, view } = rig()
    fake.answers['greeting.hello'] = e => [{ k: 'response', id: e.id!, p: { message: 5, count: 1 } }]
    await app.greet('Pico')
    expect(view.messages[0]).toMatch(/^INVALID_PAYLOAD: .*\$\.message/u)
  })
})

describe('the hello-world frontend, called by the backend', () => {
  it('asks the user when the backend asks (ui.confirm)', async () => {
    const { app, fake, view } = rig()
    let asked: Envelope | undefined
    fake.answers['greeting.ask'] = e => {
      asked = e

      return [{ k: 'request', id: 100, n: 'ui.confirm', p: { question: 'Say hello to Ada?' } }]
    }
    fake.answers['#100'] = e => [{
      k: 'response', id: asked!.id!, p: { message: (e.p as { ok: boolean }).ok ? 'Hello, Ada!' : 'Maybe later', count: 1 },
    }]
    view.answer = false
    await app.greet('Ada', true)
    expect(view.questions).toEqual(['Say hello to Ada?'])
    expect(view.messages).toEqual(['Maybe later'])
  })

  it('shows the clock', async () => {
    const { view } = rig()
    await settle()
    expect(view.ticks).toEqual([1])
  })
})
