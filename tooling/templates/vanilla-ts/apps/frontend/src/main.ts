import './style.css'
import { type NativeBridge, bridgeTransport } from '@pico-desktop/communicator'
import { App } from './app'
import { mockBackend } from './mock-backend'

declare global {
  interface Window {
    picoDesktop?: NativeBridge
  }
}

const byId = (id: string) => document.getElementById(id)!

// In Pico Desktop the native bridge; in a plain browser (`pnpm ui`) the mock backend.
const bridge = window.picoDesktop
const app = new App(bridge ? bridgeTransport(bridge) : mockBackend(), {
  state: state => { byId('state').textContent = bridge ? state : `${state} (mock backend)` },
  message: text => { byId('message').textContent = text },
  tick: n => { byId('clock').textContent = String(n) },
})
byId('greet').addEventListener('submit', event => {
  event.preventDefault()
  void app.greet((byId('name') as HTMLInputElement).value)
})
