# Pico Desktop wire protocol, version 1

This is the normative description of what the TypeScript Communicator, the C++ host and the Pico
Communicator exchange (ADR-0002). `@pico-desktop/protocol` (TS), `communicator-pico` (Pico) and
`runtime/ipc` (C++) implement it and are tested against the same vectors
(`packages/protocol/vectors/`).

## 1. Links

```text
frontend (renderer)  ⇄  host  ⇄  backend (Pico process)
        link "ui"                link "backend"
```

Each link carries a sequence of **messages**; each message is one **envelope** encoded as one CBOR
data item. Messages on a link arrive in the order they were sent; nothing is ordered across links.
The host routes between the links and may answer some messages itself (host services, F5).

- **ui link:** CEF process messages, one envelope per message as a binary value. The host knows
  which window and frame sent it; envelopes on this link carry no window field.
- **backend link:** the Pico process's stdin/stdout (ADR-0004), framed as below. The host adds the
  window field (`w`) to everything it forwards from a window, and routes the backend's messages by it.

## 2. Framing (byte-stream transports)

Each message is a **u32 big-endian payload length**, followed by that many bytes of CBOR.

- A length of 0 is invalid.
- A length above the link's limit (default **4 MiB**, configurable up to 16 MiB) is invalid.
- Bytes after the CBOR item but inside the frame are invalid.

An invalid frame is a protocol error. The receiver closes the link: a byte stream can't be resynchronized.

## 3. CBOR profile

Envelopes and payloads are CBOR (RFC 8949) restricted as follows. A receiver rejects a message that
breaks a rule, with `INVALID_MESSAGE`.

| Rule | |
|---|---|
| Definite lengths only | no indefinite-length strings, arrays or maps |
| No tags | major type 6 is rejected (bignums, dates etc. are not part of v1) |
| Simple values | `false`, `true`, `null` only; `undefined` and other simple values are rejected |
| Text | valid UTF-8 |
| Map keys | unsigned integers in the envelope, text strings everywhere in payloads; no duplicates |
| Nesting | at most 32 levels of arrays/maps (the envelope counts as one) |
| Integers | the full CBOR range, −2⁶⁴ … 2⁶⁴−1 |
| Floats | half, single and double precision are accepted |

Encoders produce the **preferred serialization**: the shortest head for every integer and length.
Integral numbers are encoded as integers, other numbers as double-precision floats. Map entries are
encoded in insertion order. With these rules, two conforming encoders produce the same bytes for
the same value, and the test vectors compare bytes.

### Language mappings

| CBOR | TypeScript | Pico |
|---|---|---|
| unsigned/negative integer | `number` when it's a safe integer (abs ≤ 2⁵³−1), else `bigint` | `Cbor.Uint(u64)` / `Cbor.Negative(u64)` (the value is −1 − n) |
| byte string | `Uint8Array` | `Cbor.Bytes(Uint8Array)` |
| text string | `string` | `Cbor.Text(string)` |
| array | `Array` | `Cbor.List(Cbor[])` |
| map (text keys) | plain object | `Cbor.Dict(Map<string, Cbor>)` |
| `false` / `true` | `boolean` | `Cbor.Bool(bool)` |
| `null` | `null` | `Cbor.Null` |
| float | `number` | `Cbor.Float(f64)` |

When encoding, TypeScript `undefined` object properties are omitted, and `undefined` elsewhere is
an error.

## 4. Envelope

A CBOR map with small unsigned-integer keys:

| Key | Field | Type | Meaning |
|---|---|---|---|
| 0 | `v` | uint | protocol version: **1** |
| 1 | `k` | uint | kind (below) |
| 2 | `id` | uint | the request ID this message is or answers |
| 3 | `n` | text | method, event or topic name, or lifecycle name |
| 4 | `p` | any | payload |
| 5 | `err` | map | error: `{0: code (text), 1: message (text), 2: data (any, optional)}` |
| 6 | `w` | uint | window ID (backend link only; set by the host) |
| 7 | `e` | uint | backend epoch (lifecycle messages) |
| 8 | `s` | uint | subscription ID |
| 9 | `d` | uint | events of this subscription dropped just before this one (absent: none) |

Receivers ignore keys they don't know (forward compatibility within a version).

### Kinds

| `k` | Kind | Required fields | Answered by |
|---|---|---|---|
| 0 | request | `id`, `n`; `p` optional | one `response` or `error` with the same `id` |
| 1 | response | `id`; `p` optional | — |
| 2 | error | `id`, `err` | — |
| 3 | event | `n`; `p` optional; `s` when it belongs to a subscription | — |
| 4 | subscribe | `id`, `n`; `p` optional (parameters) | `response` (accepted; `s` = the subscription ID) or `error` |
| 5 | unsubscribe | `s`; `err` optional | — |
| 6 | cancel | `id` (of the request to cancel) | the request still ends with its `response` or `error` (`CANCELLED`) |
| 7 | lifecycle | `n` | — |

Names are typed strings (`serial.list`, `drone.telemetry`). They're matched exactly and are at
most 256 bytes.

## 5. Requests, IDs and cancellation

- Each sender numbers its requests and subscriptions on a link: 1, 2, 3, … per epoch, never reused
  within one, and at most 2⁵³−1. A `response`/`error` carries the ID of the *peer's* request, so
  the two directions never collide.
- Only registered handlers are called. A request for an unregistered name is answered with
  `UNKNOWN_METHOD`, and an invalid payload with `INVALID_PAYLOAD`.
- A `cancel` signals the handler (AbortSignal / its Pico equivalent). It doesn't promise
  rollback, and the request still ends with exactly one `response` or `error`.
- Timeouts are the caller's: on timeout it stops waiting, sends `cancel`, and rejects locally with
  `TIMEOUT`. A late answer is dropped.

## 6. Subscriptions

A subscriber sends `subscribe` with a new `id`. The publisher accepts with `response` (`id`, `s` = the
subscription ID, chosen by the publisher) or refuses with `error`. Values then arrive as `event`s with
that `s` and the topic `n`. Either side ends a subscription with `unsubscribe` (`s`, plus `err`
when the publisher ends it because of a failure).

The publisher keeps at most **256** undelivered events per subscription. Beyond that, the oldest are
dropped, and the next delivered event says how many with `d`. The payload keeps its type. After a
backend restart, the frontend Communicator re-subscribes managed subscriptions once the
backend is `ready` again. Missed events are never replayed.

## 7. Lifecycle and handshake

The first message each side sends on a link is `lifecycle` `hello` with `p` =
`{"protocol": [min, max], "role": "frontend" | "host" | "backend", "app": text?}`. A side that can't speak any
version in the peer's range sends `error` `UNSUPPORTED_VERSION` (`id` 0) and closes the link.
Otherwise both use the highest common version.

The host tells the frontend about the backend with `lifecycle` messages carrying the epoch (`e`):

| `n` | Meaning |
|---|---|
| `ready` | the backend completed its handshake; `e` is its epoch (1, 2, … per backend start) |
| `disconnected` | the backend link is gone; pending requests were rejected with `DISCONNECTED` |
| `restarting` | the host is starting the backend again (bounded backoff) |
| `failed` | the restart budget is spent; manual action is needed |
| `closing` | the app is shutting down |

To the backend the host sends `closing` when the app shuts down. The backend should finish and exit
within the grace period, or the host terminates it (F5).

When an epoch ends, every request still pending across it is rejected with `DISCONNECTED`, and
answers from the old epoch are discarded. The host enforces both, so a frontend never sees a late reply.

## 8. Error codes

| Code | Meaning |
|---|---|
| `UNKNOWN_METHOD` | no handler is registered for the name |
| `INVALID_PAYLOAD` | the payload doesn't match the contract |
| `INVALID_MESSAGE` | the envelope or its CBOR breaks this spec |
| `MESSAGE_TOO_LARGE` | above the link's size limit |
| `UNSUPPORTED_VERSION` | no common protocol version |
| `NOT_ALLOWED` | the caller lacks the capability (F5) |
| `DISCONNECTED` | the peer went away while the request was pending |
| `TIMEOUT` | the caller gave up waiting (local only) |
| `CANCELLED` | the request was cancelled |
| `BUSY` | a queue bound was reached; nothing was done |
| `HANDLER_ERROR` | the handler failed; `err.data` may say more |
| `INTERNAL` | a bug in Pico Desktop |

Applications use their own codes as `HANDLER_ERROR` with structured `data`.

## 9. Limits (defaults)

| Limit | Default |
|---|---|
| message size | 4 MiB (configurable up to 16 MiB) |
| nesting depth | 32 |
| pending requests per link and direction | 1024 (more → `BUSY`) |
| undelivered events per subscription | 256 (oldest dropped) |
| name length | 256 bytes |
