# Pico capability report (Workstream 0)

Upstream issues for every gap below are filed on [padcom/pico-lang](https://github.com/padcom/pico-lang/issues) (#82–#88 from this audit, plus existing #14, #52, #66, #69, #74, #75).

**Date:** 2026-11-06
**Pinned compiler:** `pico 0.4.0`, built from `padcom/ppl` @ `b5a9a05` for the audit. **Now:** a local build of pico-lang `main` @ `75fc4aa` (0.4.1 plus the #82 and #84 fixes; it reports `0.4.1`). The fixes ship as **0.4.2**. `~/bin/pico` is a symlink to `ppl/target/release/pico`.
**Verified on:** Debian 13, Linux 6.12 **arm64**, Node 26.9.0. No other OS or architecture has been run yet.
**Evidence:** `probes/` (see `probes/README.md`). Every row below links to a probe that was actually compiled and run.

Status legend: **verified** = the probe passes as written · **workaround** = works, but only with the noted workaround · **missing** = not available in 0.4.0 · **unverified** = not tested yet.

## Exit gate verdict

> *Real Pico binary can exchange arbitrary binary payloads with an external parent process.*

**Met.** A release-built Pico binary exchanges length-prefixed binary frames from 0 B to 8 MB with a Node parent over stdin/stdout. It passes all 16 harness checks: ordering, fragmentation, backpressure, EOF, SIGTERM. The 0.4.0 audit needed a stdout-flush workaround (B1); since the [#82](https://github.com/padcom/pico-lang/issues/82) fix it works without one.

## Toolchain facts (executed, not guessed)

| Item | Fact |
|---|---|
| Project | `pico init scope/name` creates `project.json` (`entry: src/main.pi`). The package name must be `scope/name`. |
| Build | `pico build [--release] [--target <triple\|alias>] <file.pi> -o <out>`. It also writes `<out>.dbg` (debug info for `pico symbolize`). |
| Run | `pico run <file.pi>` |
| Tests | `pico test [paths] [-t filter] [--release]` runs `*.test.pi`, one process per file, in parallel. `std/test` provides `describe`/`it`/`expect`/hooks, Vitest-like. |
| Bench | `pico bench` runs `bench(name, fn)` in `*.bench.pi`, optimized. |
| Cross targets | `--target` with aliases such as `linux-x64`. Runtime + libc built per target, lld in-process (ppl PROGRESS P8.1). |
| Released hosts | v0.4.0 and v0.4.1 have releases for **macOS arm64, Linux x64/arm64, Windows x64/arm64**. **macOS x64 has none** (see R1, [#86](https://github.com/padcom/pico-lang/issues/86)). |
| Debugger | `pico dap` (DAP over stdio on lldb-dap) and a VS Code extension in `ppl/editors/vscode`. Not exercised yet. |
| Language server | `pico lsp` |
| Docs | `pico doc --std --out <dir>`. The spec is in `ppl/docs/spec/*.md`. |

## Capability matrix

### A. Language and core runtime

| Capability | Status | Evidence / notes |
|---|---|---|
| `Uint8Array` alloc/get/set/slice/subarray/from, 1 MB buffers | verified | `basics.test.pi > bytes` |
| 64-bit `int` (exact above 2^53), `i64` max, shifts/masks for BE packing | verified | `basics.test.pi > integers` |
| UTF-8 `TextEncoder`/`TextDecoder` | verified | `basics.test.pi > strings` |
| Classes with private state, callbacks stored in `Map<string, fn>` | verified | handler-registry pattern |
| Custom `Error` subclasses, `throws` clauses, typed `catch (e: T)` | verified | an untyped `catch (e)` is `Error` (only `message`, `cause`) |
| `async function` / `await`, top-level `await` | verified | async functions are declared `Promise<T> throws E` |
| **Async closures / async arrow functions** | **missing** | `E0900: async and generator closures isn't implemented yet`. Async work must be a named `async function` (or a method). This affects the Communicator handler API (see D1). Upstream: [#52](https://github.com/padcom/pico-lang/issues/52) |
| Manually settled `new Promise((resolve, reject) => …)` with stored resolvers | verified | request-correlation pattern |
| `setTimeout`, `delay(ms, signal)`, `AbortController` cancellation, `Promise.race` | verified | timeout/cancel pattern |
| Zero-parameter callback for `(void) => R` (e.g. `.then(() => …)`) | workaround | give the callback a parameter: `.then((v: void) => …)`. Upstream: [#88](https://github.com/padcom/pico-lang/issues/88) |
| `expect(i32)` overload ambiguity | workaround | cast: `expect(x as int)`. Upstream: [#87](https://github.com/padcom/pico-lang/issues/87) |
| Bulk byte copy | **verified (fixed)** | `u8.setAll(source, offset)` (the Web's `set(source, offset)`; named `setAll` until methods can be overloaded, ppl decision 316), `copyWithin`, and a one-copy `slice`. 1 MB: `setAll` 17 µs, `slice` 117 µs (was ~9.5 ms). Fixed: [#84](https://github.com/padcom/pico-lang/issues/84). |
| `DataView` | **missing** | pack integers by hand (shifts). Fine for CBOR. |
| `BigInt` | verified (docs) | `std/math`. Not probed; CBOR uint64 > i64 max will need it or a u64 path. |

### B. Process I/O as a child (the backend's situation)

| Capability | Status | Evidence / notes |
|---|---|---|
| Binary `stdin` read (`for await`, `read(n)`), chunks of arbitrary size | verified | harness: 1-byte dribble, straddling chunks |
| Binary `stdout.write(Uint8Array)`, in order | verified | 1000 pipelined frames in order |
| stdout delivered while the process runs | **verified (fixed)** | 0.4.0 wrote our own stdout with `fwrite(stdout)` and never flushed it, so on a pipe nothing arrived before exit. Fixed in [#82](https://github.com/padcom/pico-lang/issues/82): each `Writer.write` flushes before it settles. The probe no longer needs the stderr-write workaround. `print` is still buffered ([#14](https://github.com/padcom/pico-lang/issues/14)). |
| stderr for logs, separate from the protocol | verified | |
| Clean exit on stdin EOF (host died / closed pipe) | verified | exit 0, no leftover bytes |
| SIGTERM while blocked in a stdin read | verified | terminates in < 1 ms |
| Parent stops reading stdout (pipe backpressure) | verified | 4 MB reply intact after the reader paused for 300 ms |
| 8 MB frame | verified | 350 ms round trip (copy-bound, C1) |

### C. Performance (linux-arm64, release build, Node parent, includes Node's own cost)

| Measurement | Result |
|---|---|
| 64 B ping-pong latency (2000 sequential) | p50 **53 µs**, p95 71 µs, p99 113 µs |
| 64 B frames, pipelined | **~279,000 msg/s** (~185,000 with the 0.4.0 stderr-flush workaround) |
| 1 MB frames | ~33 MB/s each way (bound by the probe's own byte-by-byte XOR) |
| `Uint8Array` 1 MB byte-by-byte loop | ~7.5–9.5 ms |
| `u8.setAll(src)` / `copyWithin` 1 MB | ~17 µs (after #84) |
| `slice` 1 MB | ~117 µs (after #84; ~9.4 ms in 0.4.0) |

**C1 (fixed upstream, [#84](https://github.com/padcom/pico-lang/issues/84)):** in 0.4.0 every pass over a buffer cost ~9.5 ms/MB. Bulk ops now make a copy ~17 µs/MB (`setAll`) to ~117 µs/MB (`slice`, which allocates). The probe's own `FrameReader`/`encodeFrame` still copy byte by byte (the XOR transform has to), so the harness's 1 MB number is unchanged. The real codec should use `subarray` views and `setAll`.

Full log: `probes/results/stdio-harness-linux-arm64.log`.

### D. Process I/O as a parent

| Capability | Status | Evidence / notes |
|---|---|---|
| `Command(...).spawn()` with piped stdin/stdout, 200 kB binary through `cat` | verified | **you must read and write concurrently**, or both sides deadlock once the pipe buffers fill (64 kB). The same rule applies to host and backend. |
| Exit codes, `success` | verified | |
| `kill("SIGTERM")`, signal reported (`code = 128 + n`) | verified | |
| Missing program, reported as `ProcessError.kind == "NotFound"` | verified | |
| Pico parent ↔ Pico child framed protocol | verified | `process.test.pi > framed` |

### E. Networking (for pico-mavlink; not on the framework path)

| Capability | Status | Evidence / notes |
|---|---|---|
| TCP listen/accept/connect, 100 kB echo, `closeWrite` | verified | |
| TCP refused, reported as `NetError.kind == "ConnectionRefused"` | verified | |
| UDP bind/send/receive, sender address, reply-to-sender | verified | 280 B (max MAVLink 2 frame) |
| Unix-domain sockets / Windows named pipes | **missing** | `std/net` has TCP/UDP only. The IPC comparison in F2 would need FFI or loopback TCP as the alternative. Upstream: [#85](https://github.com/padcom/pico-lang/issues/85) |

### F. FFI (for pico-serialport; avoided by the framework design)

| Capability | Status | Evidence / notes |
|---|---|---|
| `extern "C" { … }` with libc functions | verified | |
| `import * as t from "c:termios.h"`: constants and functions | verified | `B115200`, `CS8`, `TCSANOW`, … |
| pty as a stand-in serial port: `posix_openpt`, `tcgetattr`, `cfmakeraw`, `cfsetspeed`, `tcsetattr`, kernel read-back | workaround (F1) | `ffi.test.pi` |
| **Passing a `Uint8Array`'s bytes to C** | **workaround (F1)** | `ptr.of(u8.buffer)` points at the ArrayBuffer **GC object**, not its bytes. Passing it to C corrupts memory: a probe that *appeared* to pass did this, and a variant **segfaulted**. Also `int as ptr<T>` is rejected (E0305). The workaround declares C pointer params as `usize` and passes `u8.buffer.__handle + u8.byteOffset`. This depends on an **internal** field, so it's fragile across compiler versions. Upstream: [#83](https://github.com/padcom/pico-lang/issues/83) |
| Structs by value, variadic C functions, calling C function pointers | missing | Upstream: [#74](https://github.com/padcom/pico-lang/issues/74), [#75](https://github.com/padcom/pico-lang/issues/75) |
| Windows COM-port APIs via FFI | unverified | |

## Risks and follow-ups raised by this audit

| ID | Item | Impact | Proposed action | Issue |
|---|---|---|---|---|
| B1 | stdout fully buffered on pipes | — | **Resolved** in the local build. `communicator-pico` needs no workaround, but requires pico ≥ 0.4.2 (or the local build). | [#82](https://github.com/padcom/pico-lang/issues/82) (closed) |
| D1 | No async closures | `communicator.handle('x', async (p) => …)` cannot be written in Pico | Pico Communicator API takes named functions/methods returning `Promise<T>`, or sync closures returning a promise. Ask upstream for a timeline. | [#52](https://github.com/padcom/pico-lang/issues/52) |
| C1 | Byte copies ~110 MB/s | — | **Resolved** in the local build. The codec uses `subarray` + `setAll`. | [#84](https://github.com/padcom/pico-lang/issues/84) (closed) |
| F1 | No supported bytes-pointer for FFI | pico-serialport depends on an internal field | Upstream API request. Isolate the `addr()` helper in one module. | [#83](https://github.com/padcom/pico-lang/issues/83) |
| E1 | No UDS / named pipes in std | F2's IPC comparison is limited | Stdio pipes stay the default. Compare against loopback TCP only. UDS via FFI only if measurements justify it. | [#85](https://github.com/padcom/pico-lang/issues/85) |
| R1 | **No macOS x64 Pico release** | Blocks one of the six targets for the 1.0.0 gate only; linux-arm64 is the current priority | Confirm with the Pico author whether `macos-x64` (cross from arm64 or a native runner) is planned. | [#86](https://github.com/padcom/pico-lang/issues/86) |
| R2 | Only linux-arm64 probed | Matrix claims are single-platform | Run `probes/` in CI on all released Pico hosts (F0 CI skeleton). | — |
| R3 | Debugger untested | VS Code story unknown | Try `pico dap` with the VS Code extension during F4. | — |
