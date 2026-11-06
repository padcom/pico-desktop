// Second page (after a same-origin navigation): a fresh bridge, the old context released, and
// no reply addressed to the first page.
import './style.css'
import { type Outcome, done, runInOrder, sleep } from './report'
import { answerTo, hello, inbox, listen, send } from './wire'

listen()

async function roundTrip(): Promise<Outcome> {
  hello()
  send({ k: 'request', id: 1, n: 'host.echo', p: 'second' })
  const answer = await answerTo(1)

  return typeof answer !== 'string' || answer
}

async function main() {
  await runInOrder([
    ['bridge again after navigation', () => typeof window.picoDesktop === 'object' || 'no bridge'],
    ['bridge round trip after navigation', roundTrip],
    ['the previous page\'s context was released', () => {
      const contexts = inbox.find(r => r.envelope.id === 1)?.liveContexts

      return contexts === 1 || `${contexts} live bridge contexts`
    }],
    ['an answer for the previous page never arrives here', async () => {
      await sleep(500)

      return !inbox.some(r => r.envelope.p === 'late') || 'the previous page\'s answer arrived'
    }],
  ])
  done()
}

void main()
