// The app's logic, without the DOM (main.ts draws it), so it's testable with a fake backend.
import { Communicator, CommunicatorError, type ConnectionState, type Transport } from '@pico-desktop/communicator'
import { type Services, validate } from './generated/app'

export interface View {
  state(state: ConnectionState): void
  message(text: string): void
  tick(n: number): void
}

export class App {
  readonly communicator: Communicator<Services>

  constructor(transport: Transport, private readonly view: View) {
    this.communicator = new Communicator<Services>({ transport, validate })
    this.communicator.onConnectionChange(state => view.state(state))
    void this.startClock()
  }

  async greet(name: string) {
    try {
      const { message } = await this.communicator.request('greeting.hello', { name })
      this.view.message(message)
    } catch (error) {
      this.view.message(error instanceof CommunicatorError ? `${error.code}: ${error.message}` : String(error))
    }
  }

  private async startClock() {
    try {
      await this.communicator.subscribe('clock.ticks', { intervalMs: 1000 }, ({ n }) => this.view.tick(n))
    } catch (error) {
      this.view.message(`the clock: ${String(error)}`)
    }
  }
}
