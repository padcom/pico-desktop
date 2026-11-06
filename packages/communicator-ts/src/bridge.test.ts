import { describe, expect, it } from 'vitest'
import { type NativeBridge, bridgeTransport } from './bridge'

function fakeBridge() {
  const sent: ArrayBuffer[] = []
  const bridge: NativeBridge = {
    hostVersion: 'test',
    send: message => sent.push(message),
    onmessage: null,
  }

  return { bridge, sent }
}

describe('bridgeTransport', () => {
  it('sends exactly the message bytes, even from a view into a larger buffer', () => {
    const { bridge, sent } = fakeBridge()
    const backing = new Uint8Array([9, 1, 2, 3, 9])
    bridgeTransport(bridge).send(backing.subarray(1, 4))
    expect(sent.map(buffer => [...new Uint8Array(buffer)])).toEqual([[1, 2, 3]])
  })

  it('delivers received buffers as bytes', () => {
    const { bridge } = fakeBridge()
    const received: Uint8Array[] = []
    bridgeTransport(bridge).connect(message => received.push(message))
    bridge.onmessage!(new Uint8Array([4, 5]).buffer, 1)
    expect(received.map(m => [...m])).toEqual([[4, 5]])
  })
})
