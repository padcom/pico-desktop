// Web-platform features that may differ between `app://` and `https://<id>.localhost` (ADR-0003).
// Reported as `pd-info:` lines: evidence for the comparison, never test failures.
type Probe = readonly [name: string, probe: () => string | Promise<string>]

export const info = (name: string, value: string) => console.log(`pd-info: ${name}=${value}`)

async function attempt([name, probe]: Probe) {
  try {
    info(name, await probe())
  } catch (error) {
    info(name, `error: ${String(error).slice(0, 400)}`)
  }
}

function openDatabase(): Promise<string> {
  return new Promise(resolve => {
    const request = indexedDB.open('pd-probe', 1)
    request.onsuccess = () => {
      request.result.close()
      resolve('works')
    }
    request.onerror = () => resolve(`error: ${request.error?.message ?? 'unknown'}`)
  })
}

async function cacheApi() {
  const cache = await caches.open('pd-probe')
  await cache.put('./probe', new Response('x'))

  return (await cache.match('./probe')) ? 'works' : 'empty'
}

async function serviceWorker() {
  if (!('serviceWorker' in navigator)) {
    return 'unavailable'
  }
  await navigator.serviceWorker.register('./sw.js')

  return 'registers'
}

const probes: Probe[] = [
  ['localStorage', () => {
    localStorage.setItem('pd', '1')

    return localStorage.getItem('pd') === '1' ? 'works' : 'lost'
  }],
  ['cookies', () => {
    document.cookie = 'pd=1; SameSite=Strict'

    return document.cookie.includes('pd=1') ? 'works' : 'not stored'
  }],
  ['indexedDB', openDatabase],
  ['serviceWorker', serviceWorker],
  ['caches', cacheApi],
  ['storage.estimate', async () => ((await navigator.storage.estimate()).quota ? 'works' : 'no quota')],
  ['clipboard API', () => (navigator.clipboard ? 'present' : 'absent')],
  ['crypto.randomUUID', () => (typeof crypto.randomUUID() === 'string' ? 'works' : 'missing')],
]

async function attemptInOrder(list: readonly Probe[]): Promise<void> {
  const [first, ...rest] = list
  if (first) {
    await attempt(first)
    await attemptInOrder(rest)
  }
}

export function probePlatform(): Promise<void> {
  return attemptInOrder(probes)
}
