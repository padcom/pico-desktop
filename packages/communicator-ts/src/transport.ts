// How a Communicator's messages travel: whole CBOR envelopes, one per call, in order. The CEF
// bridge, tests and tools each provide one.

export interface Transport {
  /** Sends one encoded envelope. */
  send(message: Uint8Array): void
  /** Starts delivering received envelopes to `receive` (called once, by the Communicator). */
  connect(receive: (message: Uint8Array) => void): void
}

/** Messages queued until `connect`, then delivered asynchronously (like a real link). */
class MemoryEnd implements Transport {
  peer: MemoryEnd | null = null
  private receive: ((message: Uint8Array) => void) | null = null
  private readonly early: Uint8Array[] = []

  send(message: Uint8Array) {
    const copy = message.slice()
    queueMicrotask(() => this.peer?.deliver(copy))
  }

  connect(receive: (message: Uint8Array) => void) {
    this.receive = receive

    for (const m of this.early.splice(0)) {
      receive(m)
    }
  }

  private deliver(message: Uint8Array) {
    if (this.receive) {
      this.receive(message)
    } else {
      this.early.push(message)
    }
  }
}

/**
 * Two connected in-memory transports (tests, and running a frontend against a mock backend).
 *
 * @returns the two ends
 */
export function memoryTransportPair(): [Transport, Transport] {
  const a = new MemoryEnd()
  const b = new MemoryEnd()
  a.peer = b
  b.peer = a

  return [a, b]
}
