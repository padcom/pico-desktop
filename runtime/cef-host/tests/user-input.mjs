// What the host does with real input, which a self-test can't produce: the window gets keys and
// clicks over the DevTools protocol, which pass through the host's handlers as the keyboard's and
// the mouse's do (a click is a user gesture). Driven with the CLI's e2e driver, under --dev.
//   - F12 opens DevTools (and another key doesn't);
//   - clicking a link to another site hands it to the system browser (a fake xdg-open records
//     it), and the window stays on the app.
// Usage: node user-input.mjs <pico-desktop-host> <fixture dist dir> <the CLI's dist/index.js>
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { pathToFileURL } from 'node:url'

const [host, fixture, cli] = process.argv.slice(2)
const { launchApp } = await import(pathToFileURL(cli).href) // (a bare path isn't a URL on Windows)

const bin = mkdtempSync(join(tmpdir(), 'pd-user-input-'))
const opened = join(bin, 'opened')
// What the host runs for an external link: xdg-open on Linux, open on macOS.
for (const name of ['xdg-open', 'open']) {
  writeFileSync(join(bin, name), `#!/bin/sh\nprintf '%s\\n' "$1" >> '${opened}'\n`, { mode: 0o755 })
}
process.env.PATH = `${bin}:${process.env.PATH}`
process.env.PICO_DESKTOP_E2E = JSON.stringify({ host, args: ['--dev', `--app-dir=${fixture}`, '--app-id=pd-user-input-test'] })

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))
const devtoolsWindows = async port => (await (await fetch(`http://127.0.0.1:${port}/json`)).json())
  .filter(t => t.url.startsWith('devtools://'))
const read = file => (existsSync(file) ? readFileSync(file, 'utf8') : '')

const app = await launchApp()
const failures = []
const check = (what, ok, detail = '') => {
  console.log(`${ok ? 'pd-input: ok' : 'FAIL:'} ${what}${ok ? '' : ` (${detail})`}`)
  if (!ok) failures.push(what)
}
try {
  // The fixture runs its own checks first, among them navigations that must have no user
  // gesture: input before they're done would give them one (Chromium's transient activation).
  await app.waitForText('#status', 'done', 30_000)
  check('DevTools is closed at first', (await devtoolsWindows(app.debuggingPort)).length === 0)
  await app.press('a')
  await sleep(500)
  check('a key other than F12 opens nothing', (await devtoolsWindows(app.debuggingPort)).length === 0)
  await app.press('F12')
  let windows = []
  for (let i = 0; i < 100 && windows.length === 0; i++) {
    await sleep(100)
    windows = await devtoolsWindows(app.debuggingPort)
  }
  check('F12 opens DevTools', windows.length === 1, `${windows.length} DevTools windows`)

  const origin = await app.evaluate('location.origin')
  const url = 'https://example.com/pd-user-input?a=1&b=two%20words'
  await app.evaluate(`document.body.insertAdjacentHTML('beforeend',
    '<a id="pd-external" href="${url}" style="display:block;padding:20px">elsewhere</a>')`)
  // Laid out before the click (a frame), and clicked again if the first one landed too early.
  await app.evaluate('new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)))')
  for (let attempt = 0; attempt < 3 && read(opened) === ''; attempt++) {
    await app.click('#pd-external')
    for (let i = 0; i < 30 && read(opened) === ''; i++) {
      await sleep(100)
    }
  }
  check('a clicked link to another site goes to the system browser, unchanged', read(opened) === `${url}\n`, JSON.stringify(read(opened)))
  check('the window stays on the app', (await app.evaluate('location.origin')) === origin)
  check('the host logs it', app.log.some(l => l.includes(`pd-host: external open ${url}`)), app.log.filter(l => l.includes('pd-host')).join(' | '))
}
catch (error) {
  check('no errors', false, error.message)
}
const code = await app.close()
check('the host exits 0', code === 0, `exit ${code}`)
rmSync(bin, { recursive: true, force: true })
process.exit(failures.length === 0 ? 0 : 1)
