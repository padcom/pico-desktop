// `vite build --mode self-test`: the page drives the real app through the host and the real Pico
// backend, then reports `pd-test:` lines (the host counts them and exits with the result). It
// covers the F3 slice: typed RPC both ways, events, a managed subscription, typed errors, and a
// backend crash and restart in the same window (host.killBackend exists only in --self-test).
import { type Communicator, CommunicatorError, type Subscription } from '@pico-desktop/communicator'
import type { Services } from './generated/hello'
import type { HelloApp } from './app'
import type { DomView } from './view'

type Outcome = true | string

interface Context {
  c: Communicator<Services>
  untyped: Communicator
  view: DomView
  states: string[]
  ticks: { epoch: number, n: number }[]
  /** Started once ready; it lasts across the backend restart. */
  clock: Promise<Subscription>
}

type Check = (t: Context) => Promise<Outcome>

const log = (line: string) => console.log(`pd-test: ${line}`)

const sleep = (ms: number) => new Promise<void>(resolve => {
  setTimeout(resolve, ms)
})

/**
 * Resolves true once `condition()` holds (checked every 25 ms), or false after `ms`.
 *
 * @param condition what to wait for
 * @param ms how long at most
 * @returns whether it came true in time
 */
function until(condition: () => boolean, ms = 5000): Promise<boolean> {
  const deadline = Date.now() + ms
  const poll = async (): Promise<boolean> => {
    if (condition()) {
      return true
    }
    if (Date.now() > deadline) {
      return false
    }
    await sleep(25)

    return poll()
  }

  return poll()
}

/**
 * The error code a call fails with.
 *
 * @param call the call
 * @returns its code, or 'resolved'
 */
async function codeOf(call: Promise<unknown>): Promise<string> {
  try {
    await call

    return 'resolved'
  } catch (error) {
    return error instanceof CommunicatorError ? error.code : String(error)
  }
}

const text = (id: string) => document.getElementById(id)?.textContent ?? ''

async function expectCode(call: Promise<unknown>, code: string): Promise<Outcome> {
  const got = await codeOf(call)

  return got === code || `got ${got}`
}

async function ready({ c }: Context): Promise<Outcome> {
  const isReady = await until(() => c.state === 'ready')

  return (isReady && c.epoch === 1) || `state ${c.state}, epoch ${c.epoch}`
}

async function clickToGreet({ view }: Context): Promise<Outcome> {
  view.name.value = 'Pico'
  document.getElementById('hello')!.click()
  const shown = await until(() => text('message') === 'Hello, Pico!' && text('count') === '1')

  return shown || `message '${text('message')}', count ${text('count')}`
}

async function picoAsks({ view }: Context): Promise<Outcome> {
  view.ask.click()

  if (!(await until(() => !view.confirmPanel.hidden))) {
    return 'no question appeared'
  }
  const question = text('question')
  view.yes.click()
  const greeted = await until(() => text('count') === '2')

  return (greeted && question === 'Say hello to Pico?') || `question '${question}', count ${text('count')}`
}

async function subscribes({ clock, ticks }: Context): Promise<Outcome> {
  await clock
  const ticked = await until(() => ticks.length >= 3)

  return ticked || `${ticks.length} ticks`
}

async function crash({ view, untyped }: Context): Promise<Outcome> {
  view.ask.click()
  await until(() => !view.confirmPanel.hidden)
  await untyped.request('host.killBackend', null)
  const failed = await until(() => text('message').startsWith('DISCONNECTED'))

  return (failed && view.confirmPanel.hidden) || `message '${text('message')}', question shown: ${!view.confirmPanel.hidden}`
}

async function restarts({ c, states }: Context): Promise<Outcome> {
  const back = await until(() => c.state === 'ready' && c.epoch === 2, 10_000)

  return (back && states.includes('disconnected') && states.includes('restarting'))
    || `state ${c.state}, epoch ${c.epoch}, went through ${states.join(' → ')}`
}

async function resumes({ ticks }: Context): Promise<Outcome> {
  const fresh = () => ticks.filter(tick => tick.epoch === 2).map(tick => tick.n)
  const resumed = await until(() => fresh().length >= 4)
  // One subscription, not two: 1, 2, 3, … with nothing repeated.
  const counted = fresh().every((n, i) => n === i + 1)

  return (resumed && counted) || `epoch-2 ticks: ${fresh().join(', ')}`
}

async function greetsAnew({ c }: Context): Promise<Outcome> {
  const { count } = await c.request('greeting.hello', { name: 'again' })

  return count === 1 || `count ${count}`
}

const CHECKS: [string, Check][] = [
  ['the backend becomes ready', ready],
  ['a click greets through Pico; Pico\'s event updates the count', clickToGreet],
  ['a typed error from Pico', ({ c }) => expectCode(c.request('greeting.hello', { name: ' ' }), 'EMPTY_NAME')],
  ['the generated Pico decoder refuses a bad payload',
    ({ untyped }) => expectCode(untyped.request('greeting.hello', { name: 5 }), 'INVALID_PAYLOAD')],
  ['an unregistered method is refused', ({ untyped }) => expectCode(untyped.request('fs.delete', null), 'UNKNOWN_METHOD')],
  ['Pico asks the page (ui.confirm) and greets after a yes', picoAsks],
  ['a managed subscription ticks', subscribes],
  ['a backend crash fails the pending call with DISCONNECTED; the window stays', crash],
  ['the backend restarts in the same window (epoch 2)', restarts],
  ['the subscription resumes by itself, once, from a fresh publisher', resumes],
  ['the new backend greets anew (nothing was replayed)', greetsAnew],
]

async function run(name: string, check: Check, t: Context) {
  try {
    const outcome = await check(t)
    log(outcome === true ? `ok ${name}` : `fail ${name}: ${outcome}`)
  } catch (error) {
    log(`fail ${name}: threw ${String(error)}`)
  }
}

/**
 * Runs the checks in order (they change the app's state), then reports `done`.
 *
 * @param app the running app
 * @param view its page
 */
export async function selfTest(app: HelloApp, view: DomView) {
  const c = app.communicator
  const states: string[] = []
  const ticks: Context['ticks'] = []
  c.onConnectionChange(state => states.push(state))
  const clock = c.subscribe('clock.ticks', { intervalMs: 50 }, ({ n }) => ticks.push({ epoch: c.epoch, n }))
  const t: Context = { c, untyped: c as unknown as Communicator, view, states, ticks, clock }
  await CHECKS.reduce((previous, [name, check]) => previous.then(() => run(name, check, t)), Promise.resolve())
  await (await clock).unsubscribe()
  log('done')
}
