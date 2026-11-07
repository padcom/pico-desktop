import { pathToFileURL } from 'node:url'
// The backend's clipboard (host.clipboard.*), checked against Chromium's own X11 clipboard: the
// backend writes and the page reads what it wrote, the page writes and the backend reads it (a
// large text too, which X11 may hand over in chunks); without the `clipboard` capability the
// backend can still write but reading is NOT_ALLOWED. Driven with the CLI's e2e driver.
// Usage: node clipboard.mjs <pico-desktop-host> <fixture dist dir> <the CLI's dist/index.js> <backend>
const [host, fixture, cli, backend] = process.argv.slice(2)
const { launchApp } = await import(pathToFileURL(cli).href) // (a bare path isn't a URL on Windows)

const failures = []
const check = (what, ok, detail = '') => {
  console.log(`${ok ? 'pd-clipboard: ok' : 'FAIL:'} ${what}${ok ? '' : ` (${detail})`}`)
  if (!ok) {
    failures.push(what)
  }
}
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))
const said = async (app, line, ms = 10_000) => {
  for (let t = 0; t < ms; t += 100) {
    if (app.log.some(l => l.includes(`clipboard-backend: ${line}`))) {
      return true
    }
    await sleep(100)
  }

  return false
}
const backendLines = app => app.log.filter(l => l.includes('clipboard-backend')).join(' | ')
const start = async (id, capabilities) => {
  process.env.PICO_DESKTOP_E2E = JSON.stringify({
    host, args: ['--dev', `--app-dir=${fixture}`, `--app-id=${id}`, `--backend=${backend}`, ...capabilities],
  })
  const app = await launchApp()
  // The fixture's own checks first; the Clipboard API wants a focused page.
  await app.waitForText('#status', 'done', 30_000)
  await app.devtools('Emulation.setFocusEmulationEnabled', { enabled: true })

  return app
}

const backendText = 'from the backend: ünïcödé ✓'
const app = await start('pd-clipboard-test', ['--capabilities=clipboard'])
try {
  check('the backend writes', await said(app, 'write ok'), backendLines(app))
  check('and reads its own text back', await said(app, `read: ${backendText}`), backendLines(app))
  const pageRead = await app.evaluate('navigator.clipboard.readText()')
  check('the page reads what the backend wrote (Chromium, through X11)', pageRead === backendText, JSON.stringify(pageRead))
  await app.evaluate('navigator.clipboard.writeText("from the page ✓")')
  check('the backend reads what the page wrote', await said(app, 'read: from the page ✓'), backendLines(app))
  await app.evaluate('navigator.clipboard.writeText("y".repeat(700000))')
  check('a large text (700 000 chars) too', await said(app, 'read: 700000 chars: yyyy'), backendLines(app))
}
catch (error) {
  check('no errors', false, error.message)
}
check('the host exits 0', (await app.close()) === 0)

const refused = await start('pd-clipboard-refused', [])
try {
  check('without the capability the backend still writes', await said(refused, 'write ok'), backendLines(refused))
  check('but reading is NOT_ALLOWED', await said(refused, 'read NOT_ALLOWED'), backendLines(refused))
}
catch (error) {
  check('no errors without the capability', false, error.message)
}
check('the host exits 0 (without the capability)', (await refused.close()) === 0)
process.exit(failures.length === 0 ? 0 : 1)
