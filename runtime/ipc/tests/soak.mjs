// A soak test of the Pico side of the link (needs Node and a built protocol package): N
// sequential echo requests of SIZE bytes to the compiled echo backend
// (packages/communicator-pico/bench/echo.pi), then the backend's peak resident memory, which must
// stay under LIMIT_KB. A runtime that keeps per-message state (pico-lang #97: the event loop's io
// map kept every read's chunk) grows with N and fails this.
// Usage: node soak.mjs <echo> [n=600] [size=1048576] [limitKb=153600]
import { spawn } from 'node:child_process'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'

const protocol = fileURLToPath(new URL('../../../packages/protocol/dist/index.js', import.meta.url))
const { FrameDecoder, decodeEnvelope, encodeEnvelope, encodeFrame } = await import(protocol)

const [exe, n = '600', size = '1048576', limit = '153600'] = process.argv.slice(2)
const child = spawn(exe, [], { stdio: ['pipe', 'pipe', 'inherit'] })
const frames = new FrameDecoder()
let answered = null
child.stdout.on('data', (data) => {
  for (const message of frames.push(new Uint8Array(data))) {
    answered?.(decodeEnvelope(message))
  }
})
const payload = new Uint8Array(Number(size)).fill(7)
for (let id = 1; id <= Number(n); id++) {
  const reply = new Promise((resolve) => {
    answered = resolve
  })
  child.stdin.write(encodeFrame(encodeEnvelope({ k: 'request', id, n: 'echo', p: payload })))
  const r = await reply
  if (r.id !== id || r.p?.length !== payload.length) {
    console.error(`FAIL: request ${id} got ${JSON.stringify({ id: r.id, length: r.p?.length })}`)
    process.exit(1)
  }
}
const peak = Number(/VmHWM:\s+(\d+)/u.exec(readFileSync(`/proc/${child.pid}/status`, 'utf8'))[1])
child.stdin.end()
console.log(`ipc.soak: ${n} × ${size} B echoed, backend peak ${peak} kB (limit ${limit})`)
process.exit(peak <= Number(limit) ? 0 : 1)
