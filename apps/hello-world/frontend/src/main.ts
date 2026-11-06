import './style.css'
import { bridgeTransport } from '@pico-desktop/communicator'
import { HelloApp } from './app'
import { DomView } from './view'

const bridge = window.picoDesktop

if (bridge) {
  const view = new DomView()
  const app = new HelloApp(bridgeTransport(bridge), view)
  view.form.addEventListener('submit', event => {
    event.preventDefault()
    void app.greet(view.name.value)
  })
  view.ask.addEventListener('click', () => void app.greet(view.name.value, true))

  // `vite build --mode self-test` only: the page tests itself (pnpm self-test).
  if (import.meta.env.MODE === 'self-test') {
    const { selfTest } = await import('./self-test')
    await selfTest(app, view)
  }
} else {
  document.getElementById('state')!.textContent = 'not running in Pico Desktop (no native bridge)'
}
