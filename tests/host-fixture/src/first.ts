// First page: assets, origin, CSP, traversal, the bridge and the navigation policy. It ends by
// sending a request and navigating away at once; the second page checks that the answer never
// reaches it.
import './style.css'
import logo from './logo.svg'
import { graphics } from './graphics'
import { probePlatform } from './probes'
import { type NamedCheck, type Outcome, runInOrder, sleep } from './report'
import { double } from './util'
import { answerTo, failsWith, hello, listen, send, sendBytes } from './wire'

const stillHere = location.href

function loadImage(src: string): Promise<Outcome> {
  return new Promise(resolve => {
    const image = new Image()
    image.onload = () => resolve(image.naturalWidth === 8 || `width ${image.naturalWidth}`)
    image.onerror = () => resolve(`couldn't load ${src}`)
    image.src = src
  })
}

function sameOriginFrame(): Promise<Outcome> {
  return new Promise(resolve => {
    const timer = setTimeout(() => resolve('the subframe never reported'), 3000)
    window.addEventListener('message', (event: MessageEvent<{ frame: string, bridge: string }>) => {
      clearTimeout(timer)
      resolve(event.data.bridge === 'undefined' || `the subframe sees picoDesktop as ${event.data.bridge}`)
    }, { once: true })
    const frame = document.createElement('iframe')
    frame.src = './frame.html'
    document.body.append(frame)
  })
}

function srcdocFrame(): Promise<Outcome> {
  return new Promise(resolve => {
    const frame = document.createElement('iframe')
    frame.srcdoc = '<p>srcdoc</p>'

    frame.onload = () => {
      const bridge = (frame.contentWindow as Window | null)?.picoDesktop
      resolve(bridge === undefined || 'the srcdoc frame has picoDesktop')
    }
    document.body.append(frame)
  })
}

const traversalAttempts = [
  './%2e%2e/%2e%2e/%2e%2e/%2e%2e/%2e%2e/etc/passwd',
  './..%2f..%2f..%2f..%2f..%2fetc%2fpasswd',
  './assets/%2e%2e%5c%2e%2e%5cpackage.json',
  './%2e%2e%2f%2e%2e%2fpackage.json',
]

async function rejectedPath(path: string): Promise<string | null> {
  const response = await fetch(path)
  const body = await response.text()
  const leaked = body.includes('root:') || body.includes('"name"')

  return response.status === 404 && !leaked ? null : `${path} answered ${response.status}`
}

async function traversal(): Promise<Outcome> {
  const problems = (await Promise.all(traversalAttempts.map(rejectedPath))).filter(p => p !== null)

  return problems.length === 0 || problems.join('; ')
}

function inlineScriptBlocked(): Promise<Outcome> {
  return new Promise(resolve => {
    let violation = false
    document.addEventListener('securitypolicyviolation', () => {
      violation = true
    }, { once: true })
    const script = document.createElement('script')
    script.textContent = 'window.inlineScriptRan = true'
    document.head.append(script)
    setTimeout(() => resolve((window.inlineScriptRan !== true && violation) || 'inline script ran'), 200)
  })
}

async function navigationBlocked(url: string, ms: number): Promise<Outcome> {
  location.href = url
  await sleep(ms)

  return location.href === stillHere || `navigated to ${location.href}`
}

function isPing(p: unknown): Outcome {
  const { text, bytes } = (p ?? {}) as { text?: string, bytes?: unknown }
  const echoed = `${text}:${bytes instanceof Uint8Array ? bytes.join() : typeof bytes}`

  return echoed === 'ping:1,2,3' || `got ${echoed}`
}

async function bridgeRoundTrip(): Promise<Outcome> {
  hello()
  send({ k: 'request', id: 1, n: 'host.echo', p: { text: 'ping', bytes: new Uint8Array([1, 2, 3]) } })
  const answer = await answerTo(1)

  return typeof answer === 'string' ? answer : isPing(answer.envelope.p)
}

async function malformedIgnored(): Promise<Outcome> {
  sendBytes(new Uint8Array([0xff]))
  // The wrong protocol version.
  sendBytes(new Uint8Array([0xa1, 0x00, 0x02]))
  send({ k: 'request', id: 4, n: 'host.echo', p: 'still here' })
  const answer = await answerTo(4)

  return typeof answer !== 'string' || answer
}

function oversizedRefused(): Outcome {
  try {
    window.picoDesktop!.send(new ArrayBuffer((4 * 1024 * 1024) + 1))

    return 'it was accepted'
  } catch (error) {
    return String(error).includes('4 MiB') || String(error)
  }
}

function bridgeReadOnly(): Outcome {
  const before = window.picoDesktop
  try {
    // @ts-expect-error -- trying to replace the bridge on purpose
    window.picoDesktop = { send: () => undefined }
  } catch {
    // expected in strict mode
  }

  return window.picoDesktop === before || 'the page replaced picoDesktop'
}

async function bundledJson(): Promise<Outcome> {
  const data = await (await fetch('./data.json')).json() as { served: string }

  return data.served === 'from the bundle' || `got ${JSON.stringify(data)}`
}

function bridgePresent(): Outcome {
  const bridge = window.picoDesktop

  return (typeof bridge === 'object' && typeof bridge.hostVersion === 'string') || 'no bridge'
}

const assets: NamedCheck[] = [
  ['trusted origin', () => {
    const expected = location.protocol === 'app:' ? 'app://pd-self-test' : 'https://pd-self-test.localhost'

    return location.origin === expected || `origin is ${location.origin}`
  }],
  ['secure context', () => (window.isSecureContext && crypto.subtle !== undefined) || 'not a secure context'],
  ['ES module import', () => double(21) === 42],
  ['dynamic import of a lazy chunk', async () => (await import('./lazy')).lazyValue === 'loaded on demand'],
  ['CSS from the bundle', () => {
    const { color } = getComputedStyle(document.querySelector('.title')!)

    return color === 'rgb(91, 76, 207)' || `color is ${color}`
  }],
  ['image asset', () => loadImage(logo)],
  ['fetch a bundled file', bundledJson],
  ['a missing file is a 404', async () => (await fetch('./missing.json')).status === 404],
]

const security: NamedCheck[] = [
  ['path traversal is rejected', traversal],
  ['CSP on HTML responses', async () => {
    const csp = (await fetch('./index.html')).headers.get('content-security-policy') ?? ''

    return csp.includes("script-src 'self'") || `CSP is '${csp}'`
  }],
  ['CSP blocks inline script', inlineScriptBlocked],
]

const bridge: NamedCheck[] = [
  ['bridge in the trusted main frame', bridgePresent],
  ['bridge round trip (CBOR, host.echo)', bridgeRoundTrip],
  ['a call without a backend fails with DISCONNECTED', () => {
    send({ k: 'request', id: 2, n: 'greeting.hello' })

    return failsWith(2, 'DISCONNECTED')
  }],
  ['a page can\'t address a window', () => {
    send({ k: 'request', id: 3, n: 'host.echo', w: 7 })

    return failsWith(3, 'INVALID_MESSAGE')
  }],
  ['malformed messages are dropped, the bridge keeps working', malformedIgnored],
  ['a message over 4 MiB is refused by the bridge', oversizedRefused],
  ['bridge is read-only', bridgeReadOnly],
  ['no bridge in a same-origin subframe', sameOriginFrame],
  ['no bridge in a srcdoc subframe', srcdocFrame],
]

const navigation: NamedCheck[] = [
  ['external navigation without a user gesture is blocked', () => navigationBlocked('https://example.com/', 500)],
  ['popup is blocked', () => window.open('https://example.com/popup') === null || 'a popup opened'],
  ['file: navigation is blocked', () => navigationBlocked('file:///etc/passwd', 300)],
]

async function main() {
  await runInOrder([...assets, ...security, ...bridge, ...navigation, ...graphics])
  await probePlatform()
  // Leave a reply in flight and replace this page: the second page must never receive it.
  send({ k: 'request', id: 99, n: 'host.echo', p: 'late' })
  location.href = './second.html'
}

listen()
void main()
