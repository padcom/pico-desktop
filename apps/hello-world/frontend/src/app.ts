// The hello-world frontend's logic, without the DOM (the View draws it): greet through Pico,
// answer Pico's ui.confirm by asking the user, show greetings as Pico announces them, and a clock
// from a managed subscription (it resumes by itself after a backend restart).
import { Communicator, CommunicatorError, type ConnectionState, type Transport } from '@pico-desktop/communicator'
import { type Services, validate } from './generated/hello'

export interface View {
  state(state: ConnectionState, epoch: number): void
  message(text: string): void
  greeted(name: string, count: number): void
  tick(n: number): void
  /** Asks the user; the signal aborts when the question is withdrawn (the backend went away). */
  confirm(question: string, signal: AbortSignal): Promise<boolean>
}

export interface AppOptions {
  /** The clock's interval in ms (1000). */
  clockMs?: number
}

const describe = (error: unknown) => (error instanceof CommunicatorError ? `${error.code}: ${error.message}` : String(error))

export class HelloApp {
  readonly communicator: Communicator<Services>

  constructor(transport: Transport, private readonly view: View, options: AppOptions = {}) {
    const c = new Communicator<Services>({ transport, validate })
    this.communicator = c
    c.onConnectionChange((state, epoch) => view.state(state, epoch))
    c.handle('ui.confirm', async ({ question }, { signal }) => ({ ok: await view.confirm(question, signal) }))
    c.on('greeting.greeted', ({ name, count }) => view.greeted(name, count))
    void this.startClock(options.clockMs ?? 1000)
  }

  private async startClock(intervalMs: number) {
    try {
      await this.communicator.subscribe('clock.ticks', { intervalMs }, ({ n }) => this.view.tick(n))
    } catch (error) {
      this.view.message(`the clock: ${describe(error)}`)
    }
  }

  /**
   * Greets `name` through Pico (`ask`: Pico asks the user first) and shows the answer.
   *
   * @param name who to greet
   * @param ask whether Pico should ask first (greeting.ask)
   */
  async greet(name: string, ask = false) {
    try {
      const { message } = await this.communicator.request(ask ? 'greeting.ask' : 'greeting.hello', { name })
      this.view.message(message)
    } catch (error) {
      this.view.message(describe(error))
    }
  }
}
