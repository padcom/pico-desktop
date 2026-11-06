import { describe, it, expect, beforeEach } from 'vitest'
import { type Envelope, decodeEnvelope, encodeEnvelope } from '@pico-desktop/protocol'
import { Communicator, CommunicatorError, memoryTransportPair } from '.'

/** The other end (host + backend), scripted by the tests. */
class Peer {
  readonly received: Envelope[] = []
  private readonly waiters: ((e: Envelope) => void)[] = []
  readonly communicator: Communicator
  private readonly sendRaw: (bytes: Uint8Array) => void

  constructor(options: { timeout?: number, maxPending?: number, validate?: (name: string, part: string) => string | null } = {}) {
    const [ours, theirs] = memoryTransportPair()
    theirs.connect(bytes => this.take(decodeEnvelope(bytes)))
    this.sendRaw = bytes => theirs.send(bytes)
    this.communicator = new Communicator({ transport: ours, ...options })
  }

  send(envelope: Omit<Envelope, 'v'>) {
    this.sendRaw(encodeEnvelope(envelope))
  }

  sendBytes(bytes: Uint8Array) {
    this.sendRaw(bytes)
  }

  ready(epoch = 1) {
    this.send({ k: 'lifecycle', n: 'ready', e: epoch })
  }

  /**
   * The next envelope the Communicator sends (ignoring its hello).
   *
   * @returns it
   */
  next(): Promise<Envelope> {
    const queued = this.received.shift()

    return queued
      ? Promise.resolve(queued)
      : new Promise(resolve => {
        this.waiters.push(resolve)
      })
  }

  private take(e: Envelope) {
    if (e.k === 'lifecycle' && e.n === 'hello') {
      return
    }
    const waiter = this.waiters.shift()
    if (waiter) {
      waiter(e)
    } else {
      this.received.push(e)
    }
  }
}

const tick = () => new Promise<void>(resolve => {
  setTimeout(resolve, 0)
})

/**
 * Waits for a promise that's expected to fail, without failing the test.
 *
 * @param promise the promise
 */
async function ignore(promise: Promise<unknown>) {
  try {
    await promise
  } catch {
    // expected
  }
}

let peer: Peer

beforeEach(() => {
  peer = new Peer()
})

describe('requests', () => {
  it('wait for the backend to be ready, then go out', async () => {
    const answer = peer.communicator.request('greeting.hello', { name: 'Ada' })
    await tick()
    expect(peer.received).toHaveLength(0)
    peer.ready()
    const request = await peer.next()
    expect(request).toMatchObject({ k: 'request', n: 'greeting.hello', p: { name: 'Ada' } })
    peer.send({ k: 'response', id: request.id!, p: 'Hello, Ada' })
    await expect(answer).resolves.toBe('Hello, Ada')
    expect(peer.communicator.state).toBe('ready')
  })

  it('are matched to answers by id, in any order', async () => {
    peer.ready()
    const a = peer.communicator.request('x', 1)
    const b = peer.communicator.request('x', 2)
    const [ra, rb] = [await peer.next(), await peer.next()]
    peer.send({ k: 'response', id: rb.id!, p: 'second' })
    peer.send({ k: 'response', id: ra.id!, p: 'first' })
    await expect(a).resolves.toBe('first')
    await expect(b).resolves.toBe('second')
  })
})

describe('request errors', () => {
  it('reject with the error the other side sent', async () => {
    peer.ready()
    const answer = peer.communicator.request('serial.lst', null)
    const request = await peer.next()
    const err = { code: 'UNKNOWN_METHOD', message: 'no handler', data: { hint: 'serial.list' } }
    peer.send({ k: 'error', id: request.id!, err })
    await expect(answer).rejects.toMatchObject({ code: 'UNKNOWN_METHOD', data: { hint: 'serial.list' } })
  })
})

describe('request failures', () => {
  it('time out, and tell the other side', async () => {
    peer = new Peer({ timeout: 20 })
    peer.ready()
    const answer = peer.communicator.request('slow', null)
    const request = await peer.next()
    await expect(answer).rejects.toMatchObject({ code: 'TIMEOUT' })
    expect(await peer.next()).toMatchObject({ k: 'cancel', id: request.id })
  })

  it('are cancelled by an AbortSignal, and a late answer is dropped', async () => {
    peer.ready()
    const controller = new AbortController()
    const answer = peer.communicator.request('slow', null, { signal: controller.signal })
    const request = await peer.next()
    controller.abort()
    await expect(answer).rejects.toMatchObject({ code: 'CANCELLED' })
    expect(await peer.next()).toMatchObject({ k: 'cancel', id: request.id })
    peer.send({ k: 'response', id: request.id!, p: 'too late' })
    await tick()
  })
})

describe('request limits', () => {
  it('fail with BUSY beyond the pending limit', async () => {
    peer = new Peer({ maxPending: 2 })
    void ignore(peer.communicator.request('a', null))
    void ignore(peer.communicator.request('a', null))
    await expect(peer.communicator.request('a', null)).rejects.toMatchObject({ code: 'BUSY' })
  })

  it('reject an answer that breaks the contract', async () => {
    peer = new Peer({ validate: (name, part) => (part === 'response' ? `${name}: expected text` : null) })
    peer.ready()
    const answer = peer.communicator.request('greeting.hello', null)
    peer.send({ k: 'response', id: (await peer.next()).id!, p: 42 })
    await expect(answer).rejects.toMatchObject({ code: 'INVALID_PAYLOAD' })
  })
})

describe('disconnects', () => {
  it('reject everything in flight with DISCONNECTED, without replaying it', async () => {
    peer.ready()
    await tick()
    const states: string[] = []
    peer.communicator.onConnectionChange(state => states.push(state))
    const answer = peer.communicator.request('x', null)
    const request = await peer.next()
    peer.send({ k: 'lifecycle', n: 'disconnected' })
    await expect(answer).rejects.toBeInstanceOf(CommunicatorError)
    await expect(answer).rejects.toMatchObject({ code: 'DISCONNECTED' })
    peer.ready(2)
    peer.send({ k: 'response', id: request.id!, p: 'from the old epoch' })
    await tick()
    expect(states).toEqual(['disconnected', 'ready'])
    expect(peer.communicator.epoch).toBe(2)
    expect(peer.received).toHaveLength(0)
  })
})

describe('invalid input', () => {
  it('drop invalid messages without failing', async () => {
    peer.ready()
    peer.sendBytes(new Uint8Array([0xff, 0x00]))
    const answer = peer.communicator.request('x', null)
    peer.send({ k: 'response', id: (await peer.next()).id!, p: 1 })
    await expect(answer).resolves.toBe(1)
  })
})
