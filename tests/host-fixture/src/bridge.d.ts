/** The native bridge the host injects into trusted main frames: protocol envelopes as ArrayBuffers. */
interface PicoDesktopBridge {
  readonly hostVersion: string
  send(message: ArrayBuffer): void
  onmessage: ((message: ArrayBuffer, liveContexts: number) => void) | null
}

interface Window {
  picoDesktop?: PicoDesktopBridge
  inlineScriptRan?: boolean
}
