// `vite build --mode self-test`: the page drives the real app through the host and the real Pico
// backend, then reports `pd-test:` lines (the host counts them and exits with the result). It
// covers the F3 slice: typed RPC both ways, events, a managed subscription, typed errors, and a
// backend crash and restart in the same window (host.killBackend exists only in --self-test).
import { type Communicator, CommunicatorError, type Subscription } from '@pico-desktop/communicator'
import { LOADS, type Load, type Sample, describe, summarize, verdict } from './bench'
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

async function retitles({ c }: Context): Promise<Outcome> {
  const { title } = await c.request('window.retitle', { title: 'Hello from Pico' })

  return title === 'Hello from Pico' || `the window says '${title}'`
}

/**
 * A permission the app didn't ask for is denied at once, not left pending: the host answers every
 * prompt (hello-world doesn't declare `clipboard`, so reading the clipboard is refused).
 *
 * @returns the outcome
 */
async function permissionAnswered(): Promise<Outcome> {
  const timeout = new Promise<string>(resolve => {
    setTimeout(() => resolve('still pending after 2 s'), 2000)
  })
  const read = (async () => {
    try {
      await navigator.clipboard.readText()

      return 'granted'
    } catch (error) {
      return (error as Error).name
    }
  })()
  const got = await Promise.race([read, timeout])

  return got === 'NotAllowedError' || got
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

/** Sizes that need a CBOR header of 1, 2, 3 and 5 bytes, up to a megabyte (a map tile is about 30 000). */
const BYTE_SIZES = [0, 1, 30_000, 1_048_576]

const counting = (n: number) => Uint8Array.from({ length: n }, (_, i) => i % 256)

const total = (bytes: Uint8Array) => bytes.reduce((sum, byte) => sum + byte, 0)

const same = (a: Uint8Array, b: Uint8Array) => a.length === b.length && a.every((byte, i) => byte === b[i])

/**
 * Whether the echo is the bytes that were sent, and Pico counted what it received.
 *
 * @param sent what the page sent
 * @param echo what came back
 * @param echo.data the bytes
 * @param echo.length how many bytes Pico counted
 * @param echo.sum their sum modulo 2^32, counted in Pico
 * @returns true, or what differs
 */
function sameBytes(sent: Uint8Array, echo: { data: Uint8Array, length: number, sum: number }): Outcome {
  if (echo.length !== sent.length || echo.sum !== total(sent) % 2 ** 32) {
    return `Pico counted ${echo.length} bytes (sum ${echo.sum}), ${sent.length} were sent (sum ${total(sent)})`
  }

  return same(sent, echo.data) || `the ${sent.length} bytes came back different`
}

/**
 * Sends `n` bytes through the whole path and back; why it didn't work, or true.
 *
 * @param c the communicator
 * @param n how many bytes
 * @returns the outcome
 */
async function echoed(c: Communicator<Services>, n: number): Promise<Outcome> {
  const sent = counting(n)
  const started = performance.now()
  const echo = await c.request('data.echo', { data: sent })
  console.log(`pd-info: data.echo of ${n} bytes: round trip ${(performance.now() - started).toFixed(2)} ms`)

  return echo.data instanceof Uint8Array ? sameBytes(sent, echo) : `${n} bytes came back as ${typeof echo.data}`
}

// A byte string (CDDL `bstr`) is bytes from the page to Pico and back: same content and count, as
// Pico itself counted them, with no text encoding in between (docs/protocol.md, "Binary payloads").
function bytesArrive({ c }: Context): Promise<Outcome> {
  const from = async (i: number): Promise<Outcome> => {
    const size = BYTE_SIZES[i]
    if (size === undefined) {
      return true
    }
    const outcome = await echoed(c, size)

    return outcome === true ? from(i + 1) : outcome
  }

  return from(0)
}

/**
 * Plays one of the benchmark's loads: the Pico backend publishes it, the page records when each event
 * arrives. Reported as a `pd-info` line, and failed only outside the (generous) limits of bench.ts,
 * with all the numbers in the message.
 *
 * @param c the communicator
 * @param load the load
 * @returns the outcome
 */
async function benchmark(c: Communicator<Services>, load: Load): Promise<Outcome> {
  const samples: Sample[] = []
  const { perSecond, count, bytes } = load
  const subscription = await c.subscribe('bench.events', { perSecond, count, bytes }, (event, delivery) => {
    samples.push({ sentAt: Number(event.sentAt), receivedAt: Date.now(), dropped: delivery.dropped })
  })
  await until(() => samples.length >= load.count, (load.count / load.perSecond) * 1000 + 5000)
  await subscription.unsubscribe()
  const summary = summarize(load, samples)
  console.log(`pd-info: bench ${describe(summary)}`)

  return verdict(summary)
}

/**
 * Round trips through the whole path (page → host → Pico → host → page): 1000 one after another,
 * then 1000 at once. Reported as `pd-info` lines (docs/ipc-benchmark.md).
 *
 * @param t the context
 * @returns true once every call came back
 */
async function throughput(t: Context): Promise<Outcome> {
  const { c } = t
  const times: number[] = []
  const sequential = async (i: number): Promise<void> => {
    if (i === 1000) {
      return
    }
    const started = performance.now()
    await c.request('greeting.hello', { name: `n${i}` })
    times.push(performance.now() - started)
    await sequential(i + 1)
  }
  await sequential(0)
  times.sort((a, b) => a - b)
  const started = performance.now()
  const all = await Promise.all(Array.from({ length: 1000 }, (_, i) => c.request('greeting.hello', { name: `p${i}` })))
  const parallelMs = performance.now() - started
  const p = (q: number) => times[Math.floor(q * (times.length - 1))]!.toFixed(2)
  console.log(`pd-info: bridge round trip p50 ${p(0.5)} ms, p95 ${p(0.95)} ms, p99 ${p(0.99)} ms (sequential)`)
  console.log(`pd-info: 1000 concurrent calls in ${parallelMs.toFixed(0)} ms (${Math.round(1000_000 / parallelMs)} calls/s)`)

  return all.length === 1000 || `${all.length} answers`
}

const CHECKS: [string, Check][] = [
  ['the backend becomes ready', ready],
  ['a click greets through Pico; Pico\'s event updates the count', clickToGreet],
  ['a typed error from Pico', ({ c }) => expectCode(c.request('greeting.hello', { name: ' ' }), 'EMPTY_NAME')],
  ['the generated Pico decoder refuses a bad payload',
    ({ untyped }) => expectCode(untyped.request('greeting.hello', { name: 5 }), 'INVALID_PAYLOAD')],
  ['an unregistered method is refused', ({ untyped }) => expectCode(untyped.request('fs.delete', null), 'UNKNOWN_METHOD')],
  ['Pico asks the page (ui.confirm) and greets after a yes', picoAsks],
  ['Pico sets the window title through the host (the window capability)', retitles],
  ['the host refuses a service the app didn\'t declare (dialogs)',
    ({ c }) => expectCode(c.request('files.pick', null), 'NOT_ALLOWED')],
  ['a permission the app didn\'t declare (clipboard read) is denied, not left pending', permissionAnswered],
  ['a managed subscription ticks', subscribes],
  ['byte strings cross the whole path as bytes, up to a megabyte, byte for byte', bytesArrive],
  ['Pico refuses text where bytes belong',
    ({ untyped }) => expectCode(untyped.request('data.echo', { data: 'AAEC' }), 'INVALID_PAYLOAD')],
  ['2000 round trips through the bridge (measured)', throughput],
  ...LOADS.map((load): [string, Check] => [
    `${load.name}: ${load.count} events at ${load.perSecond}/s, ${load.bytes} bytes each (measured)`,
    ({ c }) => benchmark(c, load),
  ]),
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
