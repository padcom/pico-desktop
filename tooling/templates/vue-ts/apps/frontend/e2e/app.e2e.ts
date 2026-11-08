// End to end, in the real window with the real Pico backend: `pnpm e2e` builds both and runs
// these (pico-desktop test --e2e). `launchApp()` starts the app; its page is driven over the
// DevTools protocol, and `app.log` has the host's output, the page's console included.
import { type E2eApp, launchApp } from 'pico-desktop'
import { afterAll, beforeAll, describe, expect, it } from 'vitest'

/**
 * The host's log lines that report a CSP violation in the page.
 *
 * @param app the running app
 * @returns the offending lines
 */
function cspViolations(app: E2eApp) {
  return app.log.filter(line => /Content Security Policy|Refused to|violates the following/iu.test(line))
}

describe('the app', () => {
  let app: E2eApp

  beforeAll(async () => {
    app = await launchApp()
  })

  afterAll(async () => {
    // Closed like its window: the backend gets `closing` and exits within the grace period.
    expect(await app.close()).toBe(0)
  })

  it('connects to the backend', async () => {
    await app.waitForText('#state', 'ready')
  })

  it('greets through the Pico backend', async () => {
    await app.fill('#name', 'end to end')
    await app.click('button[type=submit]')

    expect(await app.waitForText('#message', 'Hello')).toBe('Hello, end to end!')
  })

  it('gets the clock ticks', async () => {
    await app.waitForText('#clock', /^[1-9]/u)
  })

  it('breaks no part of the production CSP', () => {
    // The host logs the page's console; a blocked script, style or eval is an error there.
    expect(cspViolations(app)).toEqual([])
  })
})
