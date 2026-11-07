// A debugger client for debug-attach.sh, in VS Code's place: it starts `pico dap`, asks it to
// attach to the next process started from <backend> (what the template's "Debug the backend"
// does), sets a breakpoint at <source>:<line>, and when the backend stops there checks the
// frame and evaluates <expression> (which must give <expected>). Then it continues and detaches,
// leaving the backend running.
// Usage: node dap-attach.mjs <backend> <source> <line> <expression> <expected>
// Prints `dap: attaching` once it waits, and exits 0 when everything checked out.
import { spawn } from 'node:child_process'

const [backend, source, line, expression, expected] = process.argv.slice(2)
const TIMEOUT_MS = 120_000

const adapter = spawn('pico', ['dap'], { stdio: ['pipe', 'pipe', 'inherit'] })
let buffer = Buffer.alloc(0)
let seq = 0
const waiting = []
const seen = []

function fail(message) {
  console.error(`dap: FAIL: ${message}`)
  adapter.kill('SIGKILL')
  process.exit(1)
}
setTimeout(() => fail('timed out'), TIMEOUT_MS).unref()
adapter.on('exit', code => waiting.length > 0 && fail(`pico dap exited (${code})`))

adapter.stdout.on('data', (chunk) => {
  buffer = Buffer.concat([buffer, chunk])
  for (;;) {
    const end = buffer.indexOf('\r\n\r\n')
    if (end < 0) {
      return
    }
    const length = Number(/Content-Length: (\d+)/iu.exec(buffer.subarray(0, end).toString())?.[1])
    if (buffer.length < end + 4 + length) {
      return
    }
    const message = JSON.parse(buffer.subarray(end + 4, end + 4 + length).toString())
    buffer = buffer.subarray(end + 4 + length)
    if (message.type === 'event' && message.event === 'output') {
      continue
    }
    seen.push(message)
    for (const w of [...waiting]) {
      if (w.accept(message)) {
        waiting.splice(waiting.indexOf(w), 1)
        w.resolve(message)
      }
    }
  }
})

/**
 * The first message (already seen or still to come) that `accept` takes.
 * @param {(message: object) => boolean} accept which message
 * @returns {Promise<object>} the message
 */
function next(accept) {
  const index = seen.findIndex(accept)
  if (index >= 0) {
    return Promise.resolve(seen.splice(index, 1)[0])
  }

  return new Promise(resolve => waiting.push({ accept, resolve }))
}

function send(command, args = {}) {
  seq += 1
  const body = JSON.stringify({ seq, type: 'request', command, arguments: args })
  adapter.stdin.write(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`)

  return seq
}

async function request(command, args) {
  const id = send(command, args)
  const response = await next(m => m.type === 'response' && m.request_seq === id)
  if (!response.success) {
    fail(`${command}: ${response.message ?? JSON.stringify(response.body)}`)
  }

  return response
}

const event = name => next(m => m.type === 'event' && m.event === name)

await request('initialize', { adapterID: 'pico', linesStartAt1: true, columnsStartAt1: true, pathFormat: 'path' })
const attach = send('attach', { program: backend, waitFor: true })
console.log('dap: attaching')
await event('initialized')
console.log('dap: attached')
await request('setBreakpoints', { source: { path: source }, breakpoints: [{ line: Number(line) }] })
await request('configurationDone')
const attached = await next(m => m.type === 'response' && m.request_seq === attach)
if (!attached.success) {
  fail(`attach: ${attached.message}`)
}

const stopped = await event('stopped')
const { threadId } = stopped.body
const trace = await request('stackTrace', { threadId, levels: 5 })
const top = trace.body.stackFrames[0]
if (top.line !== Number(line) || !top.source?.path?.endsWith(source.split('/').pop())) {
  fail(`stopped at ${top.source?.path}:${top.line} (${top.name}), not at the breakpoint`)
}
console.log(`dap: stopped at ${top.name} (${top.source.path.split('/').pop()}:${top.line})`)
const value = await request('evaluate', { expression, frameId: top.id, context: 'watch' })
if (value.body.result !== expected) {
  fail(`${expression} is ${value.body.result}, not ${expected}`)
}
console.log(`dap: ${expression} = ${value.body.result}`)

// Holding the backend stopped for a while: with --debug the host doesn't count it as hung.
await new Promise(resolve => setTimeout(resolve, Number(process.env.PD_DAP_HOLD_MS ?? 0)))
// No breakpoint left for the page's next requests to stop at between continuing and detaching.
await request('setBreakpoints', { source: { path: source }, breakpoints: [] })
await request('continue', { threadId })
await request('disconnect', { terminateDebuggee: false })
console.log('dap: detached')
adapter.stdin.end()
process.exit(0)
