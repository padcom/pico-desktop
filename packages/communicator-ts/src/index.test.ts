import { describe, it, expect } from 'vitest'
import { PROTOCOL_VERSION as protocolVersion } from '@pico-desktop/protocol'
import { CONNECTION_STATES, PROTOCOL_VERSION } from '.'

describe('communicator', () => {
  it('re-exports the protocol version of the protocol package', () => {
    expect(PROTOCOL_VERSION).toBe(protocolVersion)
  })

  it('starts in a known state', () => {
    expect(CONNECTION_STATES[0]).toBe('connecting')
  })
})
