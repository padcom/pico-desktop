// A page that subscribes to the backend's "flood" topic (a counter every millisecond), then stops
// reading for two seconds in a busy loop, then counts what it got (tests/flood.sh). It writes the
// envelopes by hand: they are tiny, and this page has no build step.
'use strict'

const KIND_EVENT = 3
const KIND_RESPONSE = 1
const KIND_LIFECYCLE = 7
const BLOCK_MS = 2000
const LISTEN_MS = 1500

const text = (s) => Array.from(new TextEncoder().encode(s))

// {0: 1, 1: 7, 3: "hello"}
const hello = new Uint8Array([0xa3, 0x00, 0x01, 0x01, KIND_LIFECYCLE, 0x03, 0x65, ...text('hello')])
// {0: 1, 1: 4, 2: 1, 3: "flood"}: subscribe, request ID 1
const subscribe = new Uint8Array([0xa4, 0x00, 0x01, 0x01, 0x04, 0x02, 0x01, 0x03, 0x65, ...text('flood')])

/** Reads the unsigned integer or text at `at`: [value, offset after it]. */
function item(bytes, at) {
  const major = bytes[at] >> 5
  const info = bytes[at] & 31
  let length = info
  let next = at + 1
  if (info >= 24) {
    const size = 1 << (info - 24)
    length = 0
    for (let i = 0; i < size; i++) {
      length = (length * 256) + bytes[next + i]
    }
    next += size
  }
  if (major === 0) {
    return [length, next]
  }
  if (major === 3) {
    return [new TextDecoder().decode(bytes.subarray(next, next + length)), next + length]
  }
  throw new Error(`unexpected CBOR major type ${major}`)
}

/** An envelope of uint keys with unsigned or text values (what this test's messages use). */
function parse(buffer) {
  const bytes = new Uint8Array(buffer)
  const count = bytes[0] & 31  // a map of fewer than 24 entries has a one-byte head
  const fields = {}
  let at = 1
  for (let i = 0; i < count; i++) {
    const [key, afterKey] = item(bytes, at)
    const [value, afterValue] = item(bytes, afterKey)
    fields[key] = value
    at = afterValue
  }
  return fields
}

let received = 0
let dropped = 0
let last = 0
let reported = false

function report() {
  reported = true
  // The counter is 1, 2, 3, …: what arrived plus what the host said it dropped is the last number.
  console.log(`flood: received=${received} dropped=${dropped} last=${last}`)
  console.log(`flood: ${received + dropped === last ? 'accounted' : 'unaccounted'}`)
}

function block() {
  const until = Date.now() + BLOCK_MS
  while (Date.now() < until) {
    // Not reading: the renderer's thread is busy.
  }
  setTimeout(report, LISTEN_MS)
}

window.picoDesktop.onmessage = (buffer) => {
  if (reported) {
    return
  }
  const m = parse(buffer)
  if (m[1] === KIND_LIFECYCLE && m[3] === 'ready') {
    window.picoDesktop.send(subscribe.buffer)
  } else if (m[1] === KIND_RESPONSE && m[2] === 1) {
    console.log('flood: subscribed; not reading for 2 s')
    setTimeout(block, 0)
  } else if (m[1] === KIND_EVENT) {
    received += 1
    dropped += m[9] || 0
    last = m[4]
  }
}

window.picoDesktop.send(hello.buffer)
