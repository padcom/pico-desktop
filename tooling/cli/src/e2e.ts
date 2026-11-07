// End-to-end tests' view of the app (`pico-desktop test --e2e`): the real host, the built
// frontend and the compiled backend, the page driven over the DevTools protocol. A test calls
// `launchApp()`, which starts the app that `test --e2e` built, and `close()` when it's done.
import { type ChildProcessWithoutNullStreams, spawn } from 'node:child_process'
import { createServer } from 'node:net'
import { lines } from './processes'

/** What `test --e2e` hands its tests (in `PICO_DESKTOP_E2E`, as JSON). */
export interface E2eSetup {
  /** pico-desktop-host. */
  host: string
  /** Its arguments: the built app, its ID and capabilities, development mode. */
  args: string[]
}

/** The environment variable that carries the setup. */
export const E2E_SETUP = 'PICO_DESKTOP_E2E'

export interface LaunchOptions {
  /** More host arguments, after the app's (the host takes the last of a repeated option). */
  args?: string[]
  /** How long the window may take to load (default 30 s). */
  timeoutMs?: number
}

/** An error in the page, or a wait that ran out. */
export class E2eError extends Error {
  override name = 'E2eError'
}

const POLL_MS = 100

const sleep = (ms: number) => new Promise<void>(resolve => {
  setTimeout(resolve, ms)
})

/**
 * Tries again every 100 ms until `attempt` gives a value (anything but undefined).
 *
 * @param attempt one try
 * @param deadline when to give up (Date.now() time)
 * @param timedOut the error then
 * @returns the value
 */
async function poll<T>(attempt: () => Promise<T | undefined>, deadline: number, timedOut: () => Error): Promise<T> {
  const value = await attempt()
  if (value !== undefined) {
    return value
  }
  if (Date.now() > deadline) {
    throw timedOut()
  }
  await sleep(POLL_MS)

  return poll(attempt, deadline, timedOut)
}

function freePort(): Promise<number> {
  return new Promise(resolve => {
    const server = createServer().listen(0, '127.0.0.1', () => {
      const { port } = server.address() as { port: number }
      server.close(() => resolve(port))
    })
  })
}

interface Target {
  type: string
  webSocketDebuggerUrl?: string
}

/**
 * The page's DevTools WebSocket, if the host lists it yet.
 *
 * @param port the host's remote debugging port
 * @returns its URL, or undefined
 */
async function pageSocketUrl(port: number): Promise<string | undefined> {
  try {
    const response = await fetch(`http://127.0.0.1:${port}/json`)
    const targets = await response.json() as Target[]

    return targets.find(t => t.type === 'page')?.webSocketDebuggerUrl
  } catch {
    return undefined
  }
}

interface Exception {
  text: string
  exception?: { description?: string }
}

interface Reply {
  id: number
  result?: { result?: { value?: unknown }, exceptionDetails?: Exception }
  error?: { message: string }
}

const describe = (e: Exception) => e.exception?.description ?? e.text

/**
 * What went wrong in an evaluation, if anything.
 *
 * @param reply the DevTools reply
 * @param reply.result its result
 * @param reply.error its protocol error
 * @returns the message, or null
 */
function failure({ result, error }: Reply): string | null {
  const details = result?.exceptionDetails
  if (details) {
    return describe(details)
  }

  return error ? error.message : null
}

const element = (selector: string) => {
  const s = JSON.stringify(selector)

  return `(document.querySelector(${s}) ?? (() => { throw new Error('nothing matches ' + ${s}) })())`
}

/** The running app: its page, and the host's output (the page's console included). */
export class E2eApp {
  /** Everything the host printed so far, line by line; the page's console shows as `console: …`. */
  readonly log: string[] = []
  readonly #host: ChildProcessWithoutNullStreams
  readonly #exit: Promise<number>
  #socket: WebSocket | null = null
  #id = 0
  readonly #replies = new Map<number, (reply: Reply) => void>()

  /**
   * Watches a started host.
   *
   * @param host the host
   */
  constructor(host: ChildProcessWithoutNullStreams) {
    this.#host = host
    lines(host.stdout, line => this.log.push(line))
    lines(host.stderr, line => this.log.push(line))
    this.#exit = new Promise(resolve => {
      host.on('close', code => resolve(code ?? 1))
    })
  }

  /**
   * Connects to the page and waits for it to load.
   *
   * @param port the host's remote debugging port
   * @param timeoutMs how long the page may take
   */
  async connect(port: number, timeoutMs: number): Promise<void> {
    const deadline = Date.now() + timeoutMs
    const url = await poll(
      () => (this.#host.exitCode === null ? pageSocketUrl(port) : Promise.reject(new E2eError(this.#didntOpen()))),
      deadline,
      () => new E2eError(this.#didntOpen()),
    )
    const socket = new WebSocket(url)
    socket.onmessage = (message: MessageEvent) => {
      const reply = JSON.parse(String(message.data)) as Reply
      this.#replies.get(reply.id)?.(reply)
      this.#replies.delete(reply.id)
    }
    await new Promise((resolve, reject) => {
      socket.onopen = resolve
      socket.onerror = () => reject(new E2eError('cannot connect to the page'))
    })
    this.#socket = socket
    await this.waitFor('document.readyState === "complete"', Math.max(deadline - Date.now(), 1000))
  }

  /**
   * Evaluates an expression in the page (awaiting it if it's a promise).
   *
   * @param expression JavaScript
   * @returns its value (JSON-serializable values only)
   */
  evaluate<T = unknown>(expression: string): Promise<T> {
    const socket = this.#socket
    if (!socket) {
      return Promise.reject(new E2eError('not connected'))
    }
    this.#id += 1
    const id = this.#id

    return new Promise((resolve, reject) => {
      this.#replies.set(id, reply => {
        const message = failure(reply)
        if (message === null) {
          resolve(reply.result?.result?.value as T)
        } else {
          reject(new E2eError(message))
        }
      })
      const params = { expression, returnByValue: true, awaitPromise: true }
      socket.send(JSON.stringify({ id, method: 'Runtime.evaluate', params }))
    })
  }

  /**
   * The text of the first element `selector` matches ('' if none does).
   *
   * @param selector a CSS selector
   * @returns its innerText
   */
  text(selector: string): Promise<string> {
    return this.evaluate<string>(`document.querySelector(${JSON.stringify(selector)})?.innerText ?? ''`)
  }

  /**
   * Clicks the first element `selector` matches.
   *
   * @param selector a CSS selector
   */
  async click(selector: string): Promise<void> {
    await this.evaluate(`${element(selector)}.click()`)
  }

  /**
   * Types into a field: sets its value and sends `input` and `change`.
   *
   * @param selector a CSS selector
   * @param value the new value
   */
  async fill(selector: string, value: string): Promise<void> {
    await this.evaluate(`(() => { const e = ${element(selector)}; e.value = ${JSON.stringify(value)};
      e.dispatchEvent(new Event('input', { bubbles: true })); e.dispatchEvent(new Event('change', { bubbles: true })) })()`)
  }

  /**
   * Waits until an expression in the page is truthy.
   *
   * @param expression JavaScript
   * @param timeoutMs how long to wait (default 10 s)
   */
  async waitFor(expression: string, timeoutMs = 10_000): Promise<void> {
    await poll(
      async () => ((await this.evaluate<boolean>(`Boolean(${expression})`)) ? true : undefined),
      Date.now() + timeoutMs,
      () => new E2eError(`still not true after ${timeoutMs} ms: ${expression}`),
    )
  }

  /**
   * Waits until an element's text matches.
   *
   * @param selector a CSS selector
   * @param pattern what the text must contain (a string) or match (a RegExp)
   * @param timeoutMs how long to wait (default 10 s)
   * @returns the text
   */
  waitForText(selector: string, pattern: string | RegExp, timeoutMs = 10_000): Promise<string> {
    const matches = typeof pattern === 'string' ? (text: string) => text.includes(pattern) : (text: string) => pattern.test(text)
    let last = ''

    return poll(
      async () => {
        last = await this.text(selector)

        return matches(last) ? last : undefined
      },
      Date.now() + timeoutMs,
      () => new E2eError(`${selector} says ${JSON.stringify(last)}, not ${String(pattern)}, after ${timeoutMs} ms`),
    )
  }

  /**
   * Closes the app the way its window does (the backend gets `closing` and its grace period).
   *
   * @returns the host's exit code (-1 if it had to be killed after 15 s)
   */
  async close(): Promise<number> {
    this.#socket?.close()
    this.#socket = null

    if (this.#host.exitCode === null) {
      this.#host.stdin.end('quit\n')
    }
    const timer = setTimeout(() => this.#host.kill('SIGKILL'), 15_000)
    const code = await this.#exit
    clearTimeout(timer)

    return this.#host.signalCode === 'SIGKILL' ? -1 : code
  }

  #didntOpen(): string {
    return `the app's window didn't open:\n${this.log.join('\n')}`
  }
}

// Its own profile (several apps can run), and `quit` on stdin closes it.
const hostArgs = (setup: E2eSetup, port: number, more: string[] = []) => [
  ...setup.args, `--remote-debugging-port=${port}`, '--multi-instance', '--control=stdin', ...more,
]

function e2eSetup(): E2eSetup {
  const raw = process.env[E2E_SETUP]
  if (!raw) {
    throw new E2eError(`no ${E2E_SETUP}: run end-to-end tests with \`pico-desktop test --e2e\``)
  }

  return JSON.parse(raw) as E2eSetup
}

/**
 * Starts the app that `pico-desktop test --e2e` built, and connects to its page.
 *
 * @param options more host arguments, and how long the window may take
 * @returns the app
 */
export async function launchApp(options: LaunchOptions = {}): Promise<E2eApp> {
  const setup = e2eSetup()
  const port = await freePort()
  const app = new E2eApp(spawn(setup.host, hostArgs(setup, port, options.args), { stdio: ['pipe', 'pipe', 'pipe'] }))
  try {
    await app.connect(port, options.timeoutMs ?? 30_000)
  } catch (error) {
    await app.close()
    throw error
  }

  return app
}
