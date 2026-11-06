/** The native bridge the host injects into trusted main frames (F1: text echo). */
interface PicoDesktopBridge {
  readonly hostVersion: string
  send(text: string): void
  onmessage: ((text: string, liveContexts: number) => void) | null
}

interface Window {
  picoDesktop?: PicoDesktopBridge
  inlineScriptRan?: boolean
}
