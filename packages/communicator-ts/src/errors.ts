import type { CborValue, ErrorCode } from '@pico-desktop/protocol'

/** A failed call: a protocol error code (or an application's own) and an optional payload. */
export class CommunicatorError extends Error {
  constructor(readonly code: ErrorCode | (string & {}), message: string, readonly data?: CborValue) {
    super(message)
    this.name = 'CommunicatorError'
  }
}
