/// <reference types="vite/client" />
import type { NativeBridge } from '@pico-desktop/communicator'

declare global {
  interface Window {
    picoDesktop?: NativeBridge
  }
}
