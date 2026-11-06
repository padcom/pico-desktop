import { describe, expect, it } from 'vitest'
import { validate } from '../generated/experiment'

describe('the generated validate (the Communicator hook)', () => {
  it('checks each part of a service', () => {
    expect(validate('serial.open', 'request', { path: '/dev/ttyUSB0', baud: 115200 })).toBeNull()
    expect(validate('serial.open', 'request', { path: '/dev/ttyUSB0' })).toBe('$.baud: expected present')
    expect(validate('serial.open', 'response', { handle: -1 })).toMatch(/^\$\.handle: expected an unsigned integer/u)
    expect(validate('drone.telemetry', 'params', { rateHz: 300 })).toMatch(/^\$\.rateHz/u)
  })

  it('treats an absent payload as null', () => {
    expect(validate('serial.list', 'request', undefined)).toBeNull()
  })

  it('passes names and parts the contract doesn\'t have', () => {
    expect(validate('host.echo', 'request', 42)).toBeNull()
    expect(validate('ui.ready', 'response', 42)).toBeNull()
  })
})
