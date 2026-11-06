// Outgoing requests waiting for their answer: each settles exactly once (answer, error, timeout,
// cancellation or disconnect), whichever comes first.
import type { Envelope } from '@pico-desktop/protocol'
import { CommunicatorError } from './errors'

interface Entry {
  name: string
  sent: boolean
  resolve: (envelope: Envelope) => void
  reject: (error: CommunicatorError) => void
  cleanup: () => void
}

export class PendingRequests {
  private readonly entries = new Map<number, Entry>()

  get size(): number {
    return this.entries.size
  }

  add(id: number, entry: Entry) {
    this.entries.set(id, entry)
  }

  has(id: number): boolean {
    return this.entries.has(id)
  }

  name(id: number): string | undefined {
    return this.entries.get(id)?.name
  }

  markSent(id: number) {
    const entry = this.entries.get(id)
    if (entry) {
      entry.sent = true
    }
  }

  wasSent(id: number): boolean {
    return this.entries.get(id)?.sent ?? false
  }

  /**
   * Settles a request with its answer.
   *
   * @param id the request
   * @param envelope the response
   * @returns whether it was pending (late answers aren't)
   */
  resolve(id: number, envelope: Envelope): boolean {
    const entry = this.take(id)
    entry?.resolve(envelope)

    return entry !== undefined
  }

  /**
   * Settles a request with an error.
   *
   * @param id the request
   * @param error why it failed
   * @returns whether it was pending
   */
  reject(id: number, error: CommunicatorError): boolean {
    const entry = this.take(id)
    entry?.reject(error)

    return entry !== undefined
  }

  /**
   * Rejects every pending request (the backend went away).
   *
   * @param code the error code
   * @param message the error message
   */
  rejectAll(code: string, message: string) {
    for (const id of [...this.entries.keys()]) {
      this.reject(id, new CommunicatorError(code, message))
    }
  }

  private take(id: number): Entry | undefined {
    const entry = this.entries.get(id)
    if (entry) {
      this.entries.delete(id)
      entry.cleanup()
    }

    return entry
  }
}
