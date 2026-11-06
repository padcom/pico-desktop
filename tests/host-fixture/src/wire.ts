// Speaking the protocol to the host directly (no Communicator): the host answers `host.echo` in
// self-test mode, so the bridge and the router are tested without a backend.
import { type Envelope, decodeEnvelope, encodeEnvelope } from '@pico-desktop/protocol'
import { waitFor } from './report'

export interface Received {
  envelope: Envelope
  liveContexts: number
}

/** Everything this page received, in order. */
export const inbox: Received[] = []

/** Collects what the host sends (call once, first). */
export function listen() {
  if (window.picoDesktop) {
    window.picoDesktop.onmessage = (buffer, liveContexts) => {
      inbox.push({ envelope: decodeEnvelope(new Uint8Array(buffer)), liveContexts })
    }
  }
}

export function sendBytes(bytes: Uint8Array) {
  window.picoDesktop!.send(bytes.slice().buffer as ArrayBuffer)
}

export function send(envelope: Omit<Envelope, 'v'>) {
  sendBytes(encodeEnvelope(envelope))
}

export function hello() {
  send({ k: 'lifecycle', n: 'hello', p: { protocol: [1, 1], role: 'frontend' } })
}

/**
 * The answer to request `id`.
 *
 * @param id the request's ID
 * @param ms how long to wait
 * @returns the response or error, or a description of what went wrong
 */
export async function answerTo(id: number, ms = 2000): Promise<Received | string> {
  const find = () => inbox.find(r => r.envelope.id === id && (r.envelope.k === 'response' || r.envelope.k === 'error'))
  if (!(await waitFor(() => find() !== undefined, ms))) {
    return `no answer to ${id}; received ${inbox.map(r => `${r.envelope.k} ${r.envelope.id ?? r.envelope.n}`).join(', ')}`
  }

  return find()!
}

function hasCode(envelope: Envelope, code: string): true | string {
  const got = envelope.err?.code

  return got === code || `got ${envelope.k} ${got}`
}

/**
 * Checks that request `id` failed with `code`.
 *
 * @param id the request's ID
 * @param code the expected error code
 * @returns true, or what happened instead
 */
export async function failsWith(id: number, code: string): Promise<true | string> {
  const answer = await answerTo(id)

  return typeof answer === 'string' ? answer : hasCode(answer.envelope, code)
}
