// Second page (after a same-origin navigation): a fresh bridge, the old context released, and
// no reply addressed to the first page.
import './style.css'
import { done, runInOrder, sleep, waitFor } from './report'

const received: string[] = []
const counts: number[] = []

if (window.picoDesktop) {
  window.picoDesktop.onmessage = (text, liveContexts) => {
    received.push(text)
    counts.push(liveContexts)
  }
}

async function roundTrip() {
  window.picoDesktop?.send('second')

  return (await waitFor(() => received.includes('pong:second'))) || `received ${JSON.stringify(received)}`
}

async function main() {
  await runInOrder([
    ['bridge again after navigation', () => typeof window.picoDesktop === 'object' || 'no bridge'],
    ['bridge round trip after navigation', roundTrip],
    ['the previous page\'s context was released', () => {
      const contexts = counts[received.indexOf('pong:second')]

      return contexts === 1 || `${contexts} live bridge contexts`
    }],
    ['a reply for the previous page never arrives here', async () => {
      await sleep(500)

      return !received.includes('pong:late') || 'the previous page\'s reply arrived'
    }],
  ])
  done()
}

void main()
