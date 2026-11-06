// Parent-side harness for bin/stdio_echo: framed binary round trips over pipes.
// Usage: node harness/stdio-harness.mjs [path-to-binary]
import { spawn } from 'node:child_process'
import { randomBytes } from 'node:crypto'
import { performance } from 'node:perf_hooks'

const BIN = process.argv[2] ?? new URL('../bin/stdio_echo', import.meta.url).pathname
const results = []
const record = (name, ok, detail = '') => {
  results.push({ name, ok, detail })
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  — ' + detail : ''}`)
}

function frame(payload) {
  const h = Buffer.alloc(4)
  h.writeUInt32BE(payload.length)
  return Buffer.concat([h, payload])
}
const xor = b => Buffer.from(b.map(x => x ^ 0xff))

function start() {
  const child = spawn(BIN, [], { stdio: ['pipe', 'pipe', 'pipe'] })
  let buf = Buffer.alloc(0)
  const waiters = []
  const queue = []
  let stderr = ''
  child.stderr.on('data', (d) => {
    stderr += d
  })
  child.stdout.on('data', (d) => {
    buf = Buffer.concat([buf, d])
    while (buf.length >= 4) {
      const n = buf.readUInt32BE(0)
      if (buf.length < 4 + n) break
      const p = buf.subarray(4, 4 + n)
      buf = buf.subarray(4 + n)
      const w = waiters.shift()
      if (w) {
        w(Buffer.from(p))
      }
      else {
        queue.push(Buffer.from(p))
      }
    }
  })
  const exited = new Promise(resolve => child.on('exit', (code, signal) => resolve({ code, signal })))
  const recv = () => queue.length ? Promise.resolve(queue.shift()) : new Promise(resolve => waiters.push(resolve))
  const ready = new Promise((resolve) => {
    const check = () => stderr.includes('ready') ? resolve() : setTimeout(check, 2)
    check()
  })
  return { child, recv, exited, ready, stderr: () => stderr, leftover: () => buf.length }
}

async function roundTrip(p, payload) {
  p.child.stdin.write(frame(payload))
  const got = await p.recv()
  return got.equals(xor(payload))
}

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))

const pct = (xs, q) => xs.slice().sort((a, b) => a - b)[Math.floor(q * (xs.length - 1))]

async function main() {
  // 1. Sizes
  {
    const p = start()
    await p.ready
    for (const size of [0, 1, 255, 4096, 65536, 1 << 20, 8 << 20]) {
      const t0 = performance.now()
      const ok = await roundTrip(p, randomBytes(size))
      record(`round trip ${size} bytes`, ok, `${(performance.now() - t0).toFixed(1)} ms`)
    }
    p.child.stdin.end()
    const ex = await p.exited
    record('clean exit on stdin EOF', ex.code === 0 && p.leftover() === 0, `code=${ex.code} stderr=${JSON.stringify(p.stderr().trim().split('\n').pop())}`)
  }

  // 2. Pipelined ordering
  {
    const p = start()
    await p.ready
    const N = 1000
    const sent = Array.from({ length: N }, (_, i) => Buffer.from(`msg-${i}-` + 'x'.repeat(i % 50)))
    p.child.stdin.write(Buffer.concat(sent.map(frame)))
    let ok = true
    for (let i = 0; i < N; i++) {
      if (!(await p.recv()).equals(xor(sent[i]))) {
        ok = false
        break
      }
    }
    record(`${N} pipelined frames arrive complete and in order`, ok)
    p.child.stdin.end()
    await p.exited
  }

  // 3. Fragmentation: one frame dribbled byte-by-byte, plus frames coalesced across chunk boundaries
  {
    const p = start()
    await p.ready
    const payload = randomBytes(300)
    const f = frame(payload)
    for (const byte of f) {
      p.child.stdin.write(Buffer.from([byte]))
      await new Promise(resolve => setImmediate(resolve))
    }
    record('frame split into 1-byte writes', (await p.recv()).equals(xor(payload)))
    const a = randomBytes(100), b = randomBytes(200)
    const both = Buffer.concat([frame(a), frame(b)])
    p.child.stdin.write(both.subarray(0, 150))
    await sleep(10)
    p.child.stdin.write(both.subarray(150))
    const ga = await p.recv(), gb = await p.recv()
    record('frames straddling write boundaries', ga.equals(xor(a)) && gb.equals(xor(b)))
    p.child.stdin.end()
    await p.exited
  }

  // 4. Latency: sequential ping-pong, 64-byte payload
  {
    const p = start()
    await p.ready
    const lat = []
    const payload = randomBytes(64)
    for (let i = 0; i < 200; i++) await roundTrip(p, payload) // warm-up
    for (let i = 0; i < 2000; i++) {
      const t0 = performance.now()
      await roundTrip(p, payload)
      lat.push((performance.now() - t0) * 1000)
    }
    record('latency 64 B ping-pong x2000', true, `p50=${pct(lat, 0.5).toFixed(0)} µs p95=${pct(lat, 0.95).toFixed(0)} µs p99=${pct(lat, 0.99).toFixed(0)} µs`)

    // 5. Throughput: pipelined small messages and 1 MB payloads
    let t0 = performance.now()
    const M = 20000
    const small = frame(randomBytes(64))
    p.child.stdin.write(Buffer.concat(Array(M).fill(small)))
    for (let i = 0; i < M; i++) await p.recv()
    let dt = (performance.now() - t0) / 1000
    record(`throughput ${M} pipelined 64 B frames`, true, `${Math.round(M / dt)} msg/s`)

    t0 = performance.now()
    const big = randomBytes(1 << 20)
    for (let i = 0; i < 16; i++) p.child.stdin.write(frame(big))
    for (let i = 0; i < 16; i++) await p.recv()
    dt = (performance.now() - t0) / 1000
    record('throughput 16 x 1 MB frames', true, `${(16 / dt).toFixed(1)} MB/s each way`)
    p.child.stdin.end()
    await p.exited
  }

  // 6. Termination while idle
  {
    const p = start()
    await p.ready
    const t0 = performance.now()
    p.child.kill('SIGTERM')
    const ex = await p.exited
    record('SIGTERM while blocked reading stdin', ex.signal === 'SIGTERM' || ex.code !== null, `signal=${ex.signal} code=${ex.code} ${(performance.now() - t0).toFixed(1)} ms`)
  }

  // 7. Parent stops reading stdout (backpressure) — child must not lose data
  {
    const p = start()
    await p.ready
    p.child.stdout.pause()
    const payload = randomBytes(4 << 20)
    p.child.stdin.write(frame(payload))
    await sleep(300)
    p.child.stdout.resume()
    record('4 MB reply survives a paused reader (pipe backpressure)', (await p.recv()).equals(xor(payload)))
    p.child.stdin.end()
    await p.exited
  }

  const failed = results.filter(r => !r.ok).length
  console.log(`\n${results.length - failed}/${results.length} passed`)
  process.exit(failed ? 1 : 0)
}

main().catch((e) => {
  console.error(e)
  process.exit(2)
})
