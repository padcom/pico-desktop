// The page: draws what HelloApp reports and asks the user in an in-page panel (never a native
// dialog: it would block the window).
import type { ConnectionState } from '@pico-desktop/communicator'
import type { View } from './app'

const byId = <T extends HTMLElement>(id: string) => document.getElementById(id) as T

export class DomView implements View {
  readonly name = byId<HTMLInputElement>('name')
  readonly form = byId<HTMLFormElement>('greet')
  readonly ask = byId<HTMLButtonElement>('ask')
  readonly confirmPanel = byId<HTMLElement>('confirm')
  readonly yes = byId<HTMLButtonElement>('yes')
  readonly no = byId<HTMLButtonElement>('no')
  private readonly stateText = byId('state')
  private readonly messageText = byId('message')
  private readonly countText = byId('count')
  private readonly clockText = byId('clock')
  private readonly question = byId('question')

  state(state: ConnectionState, epoch: number) {
    this.stateText.textContent = epoch > 0 && state === 'ready' ? `ready (epoch ${epoch})` : state
    this.stateText.dataset.state = state
  }

  message(text: string) {
    this.messageText.textContent = text
  }

  greeted(name: string, count: number) {
    this.countText.textContent = String(count)
    this.countText.title = `last: ${name}`
  }

  tick(n: number) {
    this.clockText.textContent = String(n)
  }

  confirm(question: string, signal: AbortSignal): Promise<boolean> {
    this.question.textContent = question
    this.confirmPanel.hidden = false

    return new Promise(resolve => {
      const answer = (ok: boolean) => {
        this.confirmPanel.hidden = true
        resolve(ok)
      }
      this.yes.onclick = () => answer(true)
      this.no.onclick = () => answer(false)
      signal.addEventListener('abort', () => answer(false), { once: true })
    })
  }
}
