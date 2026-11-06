import { describe, it, expect } from 'vitest'
import { type Envelope, decodeEnvelope, encodeEnvelope } from '@pico-desktop/protocol'
import { Communicator, CommunicatorError, memoryTransportPair } from '.'

function connect(validate?: (name: string, part: string) => string | null) {
  const [ours, theirs] = memoryTransportPair()
  const received: Envelope[] = []
  theirs.connect(bytes => {
    const e = decodeEnvelope(bytes)
    if (e.n !== 'hello') {
      received.push(e)
    }
  })
  const communicator = new Communicator({ transport: ours, ...(validate ? { validate } : {}) })
  const send = (e: Omit<Envelope, 'v'>) => theirs.send(encodeEnvelope(e))
  send({ k: 'lifecycle', n: 'ready', e: 1 })

  return { communicator, received, send }
}

const settle = () => new Promise<void>(resolve => {
  setTimeout(resolve, 5)
})

describe('incoming requests', () => {
  it('call the registered handler and send its result', async () => {
    const { communicator, received, send } = connect()
    communicator.handle('ui.confirm', ({ message }: { message: string }) => message === 'Quit?')
    send({ k: 'request', id: 7, n: 'ui.confirm', p: { message: 'Quit?' } })
    await settle()
    expect(received).toEqual([{ v: 1, k: 'response', id: 7, p: true }])
  })

  it('refuse names nobody registered', async () => {
    const { received, send } = connect()
    send({ k: 'request', id: 1, n: 'fs.delete', p: {} })
    await settle()
    expect(received[0]).toMatchObject({ k: 'error', id: 1, err: { code: 'UNKNOWN_METHOD' } })
  })

  it('send a handler\'s CommunicatorError as is, other errors as HANDLER_ERROR', async () => {
    const { communicator, received, send } = connect()
    communicator.handle('a', () => {
      throw new CommunicatorError('NOT_ALLOWED', 'no', { why: 'policy' })
    })
    communicator.handle('b', () => Promise.reject(new Error('boom')))
    send({ k: 'request', id: 1, n: 'a' })
    send({ k: 'request', id: 2, n: 'b' })
    await settle()
    expect(received).toEqual([
      { v: 1, k: 'error', id: 1, err: { code: 'NOT_ALLOWED', message: 'no', data: { why: 'policy' } } },
      { v: 1, k: 'error', id: 2, err: { code: 'HANDLER_ERROR', message: 'boom' } },
    ])
  })
})

describe('incoming request checks', () => {
  it('validate the payload before the handler sees it', async () => {
    let called = false
    const validate = (_name: string, part: string) => (part === 'request' ? '$.message: expected text' : null)
    const { communicator, received, send } = connect(validate)
    communicator.handle('ui.confirm', () => {
      called = true
    })
    send({ k: 'request', id: 3, n: 'ui.confirm', p: { message: 5 } })
    await settle()
    expect(called).toBe(false)
    const err = { code: 'INVALID_PAYLOAD', message: '$.message: expected text' }
    expect(received[0]).toMatchObject({ k: 'error', id: 3, err })
  })

  it('abort the handler\'s signal on cancel', async () => {
    const { communicator, received, send } = connect()
    communicator.handle('wait', (_payload, { signal }) => new Promise((_resolve, reject) => {
      signal.addEventListener('abort', () => reject(new Error('stopped')))
    }))
    send({ k: 'request', id: 4, n: 'wait' })
    await settle()
    send({ k: 'cancel', id: 4 })
    await settle()
    expect(received[0]).toMatchObject({ k: 'error', id: 4, err: { code: 'CANCELLED' } })
  })
})

describe('events', () => {
  it('reach their listeners in order, until they stop listening', async () => {
    const { communicator, send } = connect()
    const seen: unknown[] = []
    const off = communicator.on('backend.tick', p => seen.push(p))
    send({ k: 'event', n: 'backend.tick', p: 1 })
    send({ k: 'event', n: 'backend.tick', p: 2 })
    send({ k: 'event', n: 'other', p: 3 })
    await settle()
    off()
    send({ k: 'event', n: 'backend.tick', p: 4 })
    await settle()
    expect(seen).toEqual([1, 2])
  })

  it('go out when emitted (and wait for ready before that)', async () => {
    const [ours, theirs] = memoryTransportPair()
    const received: Envelope[] = []
    theirs.connect(bytes => received.push(decodeEnvelope(bytes)))
    const communicator = new Communicator({ transport: ours })
    communicator.emit('ui.ready', {})
    await settle()
    expect(received.map(e => e.n)).toEqual(['hello'])
    theirs.send(encodeEnvelope({ k: 'lifecycle', n: 'ready', e: 1 }))
    await settle()
    expect(received.map(e => `${e.k} ${e.n}`)).toEqual(['lifecycle hello', 'event ui.ready'])
  })
})
