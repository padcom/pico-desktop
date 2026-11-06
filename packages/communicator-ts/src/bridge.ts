// The Transport over the CEF host's native bridge (`window.picoDesktop`): one ArrayBuffer of CBOR
// per envelope, each way. The host routes it to the Pico backend.
import type { Transport } from './transport'

/** The bridge the host injects into the app's trusted main frame. */
export interface NativeBridge {
  readonly hostVersion: string
  /** Sends one envelope (1 byte to 4 MiB). */
  send(message: ArrayBuffer): void
  /** Receives envelopes; the second argument is the renderer's live bridge contexts (diagnostics). */
  onmessage: ((message: ArrayBuffer, liveContexts: number) => void) | null
}

/**
 * A Transport over the host's bridge.
 *
 * @param bridge the injected bridge (`window.picoDesktop`)
 * @returns the transport
 */
export function bridgeTransport(bridge: NativeBridge): Transport {
  return {
    send(message) {
      // A copy with a buffer of its own: the bridge takes whole ArrayBuffers.
      bridge.send(message.slice().buffer as ArrayBuffer)
    },
    connect(receive) {
      bridge.onmessage = buffer => receive(new Uint8Array(buffer))
    },
  }
}
