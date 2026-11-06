# ADR-0002: CBOR envelopes; typed string-named RPC, events and managed subscriptions

**Status:** Accepted (design rounds 1–8); wire-level details are specified in F2 · **Date:** 2026-11-06

## Context
The UI and the backend have to call each other both ways, push one-way events and stream values,
in two languages, through a host that must validate and route messages without understanding
application payloads. Payloads include binary data and 64-bit integers.

## Decision
- **CBOR** is the encoding: byte strings and integers are native, unlike JSON.
- A versioned **envelope**: `{ version, kind, session, id?, method?, event?, subscription?,
  payload?, error? }`, `kind` one of `request | response | error | event | subscribe |
  unsubscribe | cancel | lifecycle` (in `@pico-desktop/protocol` as `MESSAGE_KINDS`).
- Method and event names are **typed strings** (`communicator.request('serial.list', {})`); no
  generated fluent proxies are required.
- Only **explicitly registered handlers** are callable, on both sides.
- In-flight calls reject with `DISCONNECTED` when the backend session ends; nothing is retried or
  replayed. Session epochs discard late replies. Subscriptions are restored after an expected
  restart, without pretending missed events were delivered.
- Timeouts and cancellation (`AbortSignal` in TypeScript, the Pico equivalent) end the wait and
  signal the handler; they don't promise rollback.

## Specified (F2)
Map keys and types, the CBOR profile, numeric widths, size limits, queue bounds, request-ID
allocation, ordering, error codes, the handshake and compatibility rules are in
[`docs/protocol.md`](../protocol.md) (version 1), with shared vectors in
`packages/protocol/vectors/`.

## Consequences
- The host decodes just enough of the envelope to route and validate.
- A Pico CBOR codec is needed. Pico has no `DataView`, so integers are packed by hand (verified)
  and floats go through `Float64Array` views; bulk copies use `Uint8Array.setAll`/`slice` (#84).
- Pico has no async closures (pico-lang #52), so Pico handlers are named async functions or
  methods.
