// Reports checks to the host as `pd-test:` console lines (the host counts them and exits with
// the result). A check returns true, or a description of what went wrong.
export type Outcome = boolean | string

export type Check = () => Outcome | Promise<Outcome>

export type NamedCheck = readonly [name: string, check: Check]

const log = (line: string) => console.log(`pd-test: ${line}`)

const describe = (outcome: Outcome) => (outcome === false ? 'false' : String(outcome))

export async function check(name: string, fn: Check) {
  try {
    const outcome = await fn()
    log(outcome === true ? `ok ${name}` : `fail ${name}: ${describe(outcome)}`)
  } catch (error) {
    log(`fail ${name}: threw ${String(error)}`)
  }
}

/**
 * Runs checks one after another (they may change page state, so never in parallel).
 *
 * @param checks the named checks, in order
 * @returns once every check has reported
 */
export async function runInOrder(checks: readonly NamedCheck[]): Promise<void> {
  const [first, ...rest] = checks
  if (first) {
    await check(...first)
    await runInOrder(rest)
  }
}

export const sleep = (ms: number) => new Promise<void>(resolve => {
  setTimeout(resolve, ms)
})

/**
 * Resolves true once `condition()` holds, or false after `ms`.
 *
 * @param condition checked every 50 ms
 * @param ms how long to wait at most
 * @returns whether the condition came true in time
 */
export function waitFor(condition: () => boolean, ms = 2000): Promise<boolean> {
  return new Promise(resolve => {
    const started = Date.now()
    const poll = () => {
      if (condition()) {
        resolve(true)
      } else if (Date.now() - started > ms) {
        resolve(false)
      } else {
        setTimeout(poll, 50)
      }
    }
    poll()
  })
}

/**
 * The next bridge message, or a failure description after `ms`.
 *
 * @param ms how long to wait for it
 * @returns the message and the renderer's live bridge contexts, or what went wrong
 */
export function nextBridgeMessage(ms = 2000): Promise<{ text: string, liveContexts: number } | string> {
  return new Promise(resolve => {
    const bridge = window.picoDesktop
    if (!bridge) {
      resolve('no bridge')

      return
    }
    const timer = setTimeout(() => resolve('no reply within the timeout'), ms)
    bridge.onmessage = (text, liveContexts) => {
      clearTimeout(timer)
      resolve({ text, liveContexts })
    }
  })
}

export function done() {
  document.getElementById('status')!.textContent = 'done'
  log('done')
}
