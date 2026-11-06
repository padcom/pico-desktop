import { describe, it, expect } from 'vitest'
import { FRAME_HEADER_BYTES, MESSAGE_KINDS, PROTOCOL_VERSION, isMessageKind } from '.'

describe('protocol', () => {
  it('has an integer version', () => {
    expect(Number.isInteger(PROTOCOL_VERSION)).toBe(true)
  })

  it('lists each message kind once', () => {
    expect(new Set(MESSAGE_KINDS).size).toBe(MESSAGE_KINDS.length)
  })

  it('recognizes message kinds', () => {
    expect(isMessageKind('request')).toBe(true)
    expect(isMessageKind('reply')).toBe(false)
  })

  it('frames with a 4-byte length header', () => {
    expect(FRAME_HEADER_BYTES).toBe(4)
  })
})
