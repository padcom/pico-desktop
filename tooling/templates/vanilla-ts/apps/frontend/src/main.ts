import './style.css'
import { type NativeBridge, bridgeTransport } from '@pico-desktop/communicator'
import { App } from './app'

declare global {
  interface Window {
    picoDesktop?: NativeBridge
  }
}

const byId = (id: string) => document.getElementById(id)!

if (window.picoDesktop) {
  const app = new App(bridgeTransport(window.picoDesktop), {
    state: state => { byId('state').textContent = state },
    message: text => { byId('message').textContent = text },
    tick: n => { byId('clock').textContent = String(n) },
  })
  byId('greet').addEventListener('submit', event => {
    event.preventDefault()
    void app.greet((byId('name') as HTMLInputElement).value)
  })
} else {
  byId('state').textContent = 'not running in Pico Desktop (no native bridge)'
}
