# ADR-0004: Length-prefixed stdin/stdout pipes between the host and Pico

**Status:** Proposed; decided by the F2 experiment · **Date:** 2026-11-06

## Context
The host and the Pico process exchange CBOR envelopes (ADR-0002). The candidates are child
stdin/stdout pipes, Unix-domain sockets / Windows named pipes, and loopback TCP (as a control
only: no listening port in production).

## Evidence so far (Workstream 0, linux-arm64; `docs/pico-capabilities.md`)
- Framed binary stdio works from 0 B to 8 MB: ordering, fragmentation, backpressure, EOF and
  SIGTERM all pass. 64 B ping-pong p50 53 µs, ~279k pipelined msg/s.
- That needed one compiler fix: stdout wasn't flushed on pipes in 0.4.0 (pico-lang #82, fixed
  for 0.4.2).
- Pico's std library has no Unix-domain sockets or named pipes (pico-lang #85), so those
  candidates would need FFI today.

## Decision (working default)
**Framed pipes**: each message is a u32 big-endian length plus the CBOR payload
(`FRAME_HEADER_BYTES` in `@pico-desktop/protocol`, `pico_desktop::ipc` in C++). stdout carries
only frames; Pico logs go to stderr. The transport stays behind a replaceable adapter on both
sides.

F2 still runs the full comparison (small/64 KB/1 MB messages, 10–100 concurrent calls,
saturation, crashes, hangs, reconnects; p50/p95, CPU, memory) per OS family, and records the
outcome here.

## Consequences
- No socket path or port to secure; the child's pipes are private to the host.
- Host and backend must read and write concurrently (a blocking write bigger than the pipe buffer
  deadlocks: observed in the audit).
- Requires pico ≥ 0.4.2 (or the local build with #82).
