import { describe, it, expect } from 'vitest'
import { type Envelope, decodeEnvelope, encodeEnvelope } from '@pico-desktop/protocol'
import { Communicator, memoryTransportPair } from '.'

/**
 * A publisher on the other end: accepts every subscription with a fresh `s`.
 *
 * @returns the Communicator under test and the publisher's side
 */
function publisher() {
  const [ours, theirs] = memoryTransportPair()
  const received: Envelope[] = []
  let nextS = 100
  const send = (e: Omit<Envelope, 'v'>) => theirs.send(encodeEnvelope(e))
  theirs.connect(bytes => {
    const e = decodeEnvelope(bytes)
    received.push(e)

    if (e.k === 'subscribe') {
      send({ k: 'response', id: e.id!, s: nextS++ })
    }
  })
  const communicator = new Communicator({ transport: ours })
  send({ k: 'lifecycle', n: 'ready', e: 1 })

  return { communicator, received, send }
}

const settle = () => new Promise<void>(resolve => {
  setTimeout(resolve, 5)
})

describe('subscriptions', () => {
  it('deliver values for their subscription id, with drop counts', async () => {
    const { communicator, send } = publisher()
    const values: [unknown, number][] = []
    const subscription = await communicator.subscribe('drone.telemetry', { rateHz: 10 }, (v, d) => values.push([v, d.dropped]))
    send({ k: 'event', n: 'drone.telemetry', s: subscription.current!, p: { alt: 1 } })
    send({ k: 'event', n: 'drone.telemetry', s: subscription.current!, d: 3, p: { alt: 2 } })
    send({ k: 'event', n: 'drone.telemetry', s: 999, p: { alt: 'someone else' } })
    await settle()
    expect(values).toEqual([[{ alt: 1 }, 0], [{ alt: 2 }, 3]])
  })

  it('send unsubscribe, after which values are ignored', async () => {
    const { communicator, received, send } = publisher()
    const values: unknown[] = []
    const subscription = await communicator.subscribe('clock', undefined, v => values.push(v))
    const s = subscription.current!
    await subscription.unsubscribe()
    send({ k: 'event', n: 'clock', s, p: 1 })
    await settle()
    expect(values).toEqual([])
    expect(received.at(-1)).toMatchObject({ k: 'unsubscribe', s })
    expect(subscription.active).toBe(false)
  })
})

describe('subscription timing', () => {
  it('get a value that follows the acceptance immediately', async () => {
    const [ours, theirs] = memoryTransportPair()
    const send = (e: Omit<Envelope, 'v'>) => theirs.send(encodeEnvelope(e))
    theirs.connect(bytes => {
      const e = decodeEnvelope(bytes)
      if (e.k === 'subscribe') {
        send({ k: 'response', id: e.id!, s: 7 })
        send({ k: 'event', n: 'clock', s: 7, p: 1 })
      }
    })
    const communicator = new Communicator({ transport: ours })
    send({ k: 'lifecycle', n: 'ready', e: 1 })
    const values: unknown[] = []
    await communicator.subscribe('clock', undefined, v => values.push(v))
    await settle()
    expect(values).toEqual([1])
  })
})

describe('subscription lifetimes', () => {
  it('end when the publisher ends them', async () => {
    const { communicator, send } = publisher()
    const ended: unknown[] = []
    const subscription = await communicator.subscribe('drone.telemetry', {}, () => undefined, e => ended.push(e?.code ?? null))
    send({ k: 'unsubscribe', s: subscription.current!, err: { code: 'DISCONNECTED', message: 'vehicle lost' } })
    await settle()
    expect(ended).toEqual(['DISCONNECTED'])
    expect(subscription.active).toBe(false)
  })

  it('subscribe again after a backend restart, exactly once, without replay', async () => {
    const { communicator, received, send } = publisher()
    const values: unknown[] = []
    const subscription = await communicator.subscribe('clock', { every: 1 }, v => values.push(v))
    const first = subscription.current!
    send({ k: 'lifecycle', n: 'disconnected' })
    send({ k: 'lifecycle', n: 'ready', e: 2 })
    await settle()
    const subscribes = received.filter(e => e.k === 'subscribe')
    expect(subscribes).toHaveLength(2)
    expect(subscribes[1]).toMatchObject({ n: 'clock', p: { every: 1 } })
    expect(subscription.current).not.toBe(first)
    send({ k: 'event', n: 'clock', s: first, p: 'stale' })
    send({ k: 'event', n: 'clock', s: subscription.current!, p: 'fresh' })
    await settle()
    expect(values).toEqual(['fresh'])
  })
})

describe('publishing', () => {
  it('are refused by a frontend asked to publish', async () => {
    const { received, send } = publisher()
    send({ k: 'subscribe', id: 5, n: 'ui.theme' })
    await settle()
    expect(received.at(-1)).toMatchObject({ k: 'error', id: 5, err: { code: 'UNKNOWN_METHOD' } })
  })
})
