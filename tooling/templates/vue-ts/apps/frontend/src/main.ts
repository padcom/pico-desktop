import './style.css'
import { type NativeBridge, bridgeTransport } from '@pico-desktop/communicator'
import { createApp } from 'vue'
import App from './App.vue'
import { mockBackend } from './mock-backend'

declare global {
  interface Window {
    picoDesktop?: NativeBridge
  }
}

// In Pico Desktop the native bridge; in a plain browser (`pnpm ui`) the mock backend.
const bridge = window.picoDesktop
createApp(App, { transport: bridge ? bridgeTransport(bridge) : mockBackend(), mock: !bridge }).mount('#app')
