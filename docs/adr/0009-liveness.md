# ADR-0009: Liveness by ping/pong, enforced only for backends that answer

**Status:** Accepted (F5, 2026-11-07) · **Date:** 2026-11-07

## Context
A backend whose event loop stops (a synchronous loop, a deadlock) keeps its pipes open, and the
IPC benchmark showed nothing notices that except request timeouts (docs/ipc-benchmark.md). The
host must restart it like a crashed backend (PLAN.md F5). Protocol v1 is frozen, and additions
must not break older peers (docs/versioning.md).

## Decision
- While the backend is ready, the host sends `lifecycle` `ping` (`p` = a sequence number) every 5 s.
  The backend answers `lifecycle` `pong` with the same `p`.
- Once a backend has answered one ping, 15 s without a `pong` is a hang: the host kills it, and the
  restart policy takes over (backoff, at most 5 restarts in a row, then `failed`).
- A backend that never answers a ping is never judged by it. Receivers ignore lifecycle names they
  don't know, so this fits within protocol v1.
- The timings are host options (`--liveness`); tests use shorter ones.

## Consequences
- The Pico Communicator answers pings in its event loop. A handler that blocks the loop for more
  than 15 s now gets its backend restarted, which is the point, so long CPU-bound work has to yield.
- Pages are never pinged; a page that hangs is Chromium's business.
