# ADR-0004: Length-prefixed stdin/stdout pipes between the host and Pico

**Status:** Accepted (F2 benchmark, 2026-11-06; linux-arm64; macOS/Windows numbers follow with F6) · **Date:** 2026-11-06

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

## Benchmark (F2, `docs/ipc-benchmark.md`)
The real protocol code ran on both sides: the host's C++ framing and envelope reader, and a Pico echo backend. Pipes against loopback TCP (control):
- 64 B round trip p50: **48 µs vs 82 µs**;
- 1 MiB: 404 vs 547 MB/s;
- pipelined throughput: equal, and limited by the backend;
- a killed backend is seen as EOF within 0.1 ms; a hung one only by a timeout.

## Decision
**Framed stdin/stdout pipes.** Each message is a u32 big-endian length plus the CBOR payload
(`FRAME_HEADER_BYTES` in `@pico-desktop/protocol`, `pico_desktop::ipc` in C++, `FrameReader` in
communicator-pico). stdout carries only frames; Pico logs go to stderr. Reasons:
- **Lower latency**, the cost that matters for RPC.
- **Nothing to secure:** no port, no socket path; the pipes are private to the host and child.
- **Free crash detection:** EOF the moment the backend dies.
- **Free shutdown:** closing stdin is the shutdown signal a backend can't miss.
- **Portable:** works with Pico's standard library on every target. Unix-domain sockets and named pipes would need FFI (#85); TCP needs `TCP_NODELAY` on the Pico side (#65) to be competitive and opens a listening port.

The transport stays behind a replaceable adapter on both sides; a measured need (for example, a
second channel for bulk data) would add one, not replace this.

## Consequences
- Host and backend must read and write concurrently. A blocking write bigger than the pipe buffer
  deadlocks (observed in the audit); the host writes from its own thread.
- A hung backend is only detected by request timeouts and health checks (F5).
- Requires pico ≥ 0.4.2 (stdout flush, #82). Long-running backends also need #90 (finalizers and
  buffer memory) before release.
