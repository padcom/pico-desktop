# Host ↔ Pico IPC benchmark (ADR-0004)

`runtime/ipc/bench/ipc_bench.cpp` is the host side of the backend link. It drives a Pico echo backend
(`packages/communicator-pico/bench/echo.pi`) that answers each request through the real
protocol code: frame reader, envelope decode into the typed struct, envelope encode, frame. The
replies to one input chunk go out in one write.

```sh
pico build --release packages/communicator-pico/bench/echo.pi -o build/bench/echo
cmake --build --preset default && build/default/runtime/ipc/pd_ipc_bench build/bench/echo
```

## Results: linux-arm64

The machine is Debian 13 (Linux 6.12, arm64 VM, 12 cores). The Pico compiler is pico-lang `main` @ `75fc4aa`
(0.4.1 + #82 + #84), release build. Three full runs agreed within a few percent; this is run 2:

| transport | 64 B round trip p50 / p95 / p99 | 64 KiB p50 | 1 MiB p50 | msg/s, 1 in flight | 10 in flight | 100 in flight |
|---|---|---|---|---|---|---|
| stdin/stdout pipes | **48** / 101 / 195 µs | 196 µs (300 MB/s) | 2.5 ms (404 MB/s) | **16 344** | 40 063 | 25 747 |
| loopback TCP | 82 / 134 / 255 µs | 204 µs (289 MB/s) | 1.8 ms (547 MB/s) | 10 268 | 36 211 | 25 605 |

- **Latency:** pipes are ~40 % faster at small messages (48 vs 82 µs p50), which is what RPC is bound by. TCP pays for the loopback stack, and Pico can't set `TCP_NODELAY` on its side (`std/net` has no socket options yet, pico-lang #65).
- **Bulk:** TCP moves 1 MiB messages faster (larger socket buffers). At 400 MB/s, pipes are far beyond what the UI link needs.
- **Throughput:** pipelined throughput is the same for both and is limited by the backend, not the transport (see the next section).

**After three runtime fixes (2026-11-07):** pico-lang `main` 70c61e6 plus PR #94, PR #96 (#95: buffers in synchronous code) and PR #98 (#97: `Map` kept deleted entries), release build:

| transport | 64 B round trip p50 / p95 / p99 | 64 KiB p50 | 1 MiB p50 | msg/s, 1 in flight | 10 in flight | 100 in flight |
|---|---|---|---|---|---|---|
| stdin/stdout pipes | **44** / 78 / 131 µs | 142 µs (425 MB/s) | 2.0 ms (**501 MB/s**) | **20 592** | **84 784** | **144 068** |
| loopback TCP | 81 / 118 / 177 µs | 148 µs (402 MB/s) | 1.2 ms (816 MB/s) | 11 852 | 69 503 | 140 316 |

Backend CPU for the whole run fell from 6.3 s to 2.4 s, and its peak RSS from ~410 MB to **50 MB**. The pipelined numbers aren't limited by the backend's bookkeeping any more. The cause is described under "What the benchmark found in Pico" below.

**The whole path** (page → renderer → browser process → Pico → back, through CEF's process messages), measured by hello-world's self-test (`pd-info` lines): sequential round trips p50 **0.20 ms**, p95 0.40 ms, p99 0.70 ms; 1000 concurrent calls take 44 ms (**~23k calls/s**). End to end, the bridge is the limit, not the Pico link (144k msg/s); that's ample for a UI.

| failure | observed (pipes) |
|---|---|
| backend killed (SIGKILL) with a request pending | EOF on the pipe after **0.07–0.14 ms** |
| backend hung (SIGSTOP) | the link stays open and silent: **only a timeout notices** |
| clean shutdown (close stdin, wait for exit) | **0.3–0.4 ms** |

With pipes, a crash is detected immediately. A hang is noticed by the host's liveness pings since
F5: `lifecycle` `ping`/`pong` every 5 s, and a hang after 15 s of silence (docs/protocol.md §7,
ctest `host.hang`). Host-side cost was negligible (0.8 s CPU and 8 MB RSS for the whole run, against the backend's 6.5 s).

## What the benchmark found in Pico: pico-lang #90

Backend memory grew with the number of messages (peak RSS **780 MB** for an echo), and 100-in-flight
throughput fell from ~100k msg/s in short runs to ~26k in long ones. In 0.4.1:
- the event loop runs finalization callbacks only once it's idle, which a backend never is, so `ArrayBuffer` memory was never freed;
- collections are triggered by GC-heap allocation only, which leaves off-heap buffer memory out of the picture.

pico-lang #90 has the analysis. The fix runs due finalizers between busy loop steps and counts buffer memory toward the triggers; it was merged (PR #91) on 2026-11-07. With it:
- a tight allocation loop stays at ~120 MB instead of growing past 660 MB;
- 100-in-flight throughput stays at ~50k msg/s;
- peak RSS is still ~410 MB, mostly from the 1 MiB phase (re-measured 2026-11-07 with main: 413 MB; 1 MiB p50 4.0 ms, 256 MB/s), so more work is needed there.

This doesn't depend on the transport: both transports showed the same growth.

**The rest: pico-lang #97 (2026-11-07).** The remaining ~410 MB grew linearly with the number of requests, about 1 MiB kept per 1 MiB echoed, and the code under test wasn't the cause. `Map.delete` kept a deleted entry, key and value, until a rehash, and rehashes always doubled the buckets. The event loop's `io` map has one entry per I/O job, so it kept every read's promise and its 64 KiB chunk: a program reading stdin kept about half of everything it had read. PR #98 fixes it, and the backend now stays at 20 MB after 400 × 1 MiB requests. The pico-desktop Communicator's own maps (running handlers, waiting requests) churn the same way and get the fix too. #95 (PR #96) is separate: buffers in long synchronous code.

## Map tiles and telemetry through the whole path (#50)

The parity work will push two kinds of traffic from the backend to the page: **map tiles** (256 × 256, about
30 KB, 50 a second) and **telemetry** events (100 a second). hello-world's self-test plays both through the
real path (Pico backend → pipe → host → renderer → page) and checks the result. This is the number that
matters for the user: not the Pico link alone (the table above) but what reaches the page, and when.

- **The load.** `bench.events` (hello-world's contract, `apps/hello-world/contracts/hello.cddl`) is a
  subscription: the backend publishes `count` events, `perSecond` of them every second, each carrying
  `bytes` bytes and the time it was sent. The page records the time each one arrives and the `dropped`
  count the publisher puts on an event after it dropped some (docs/protocol.md, "Backpressure").
  Loads (`apps/hello-world/frontend/src/bench.ts`): *map tiles* 150 events of 30 000 bytes at 50/s, and
  *telemetry* 300 events of 64 bytes at 100/s, one after the other, each about 3 seconds.
- **Latency** is the page's `Date.now()` at arrival minus the backend's `Date.now()` at sending: the same
  machine's clock, so it is meaningful to about a millisecond. Reported are p50, p95, p99 and the maximum.
- **Lost** is `count` minus what arrived. **Dropped by the publisher** is the sum of the `d` the page saw.
  With a page that keeps up, both should be 0.
- **The gate.** On Linux the self-test fails when more than 2 % are lost, the p95 latency is above 500 ms,
  or the maximum above 2 s (`LIMITS` in `bench.ts`). They are far above what a healthy run does, on
  purpose: CI runners differ, and a flaky gate costs more than it catches. A failure prints every number on
  one line. **On macOS and Windows the numbers are only reported** (`reported only on this system`), until
  their first results are seen: the limits were chosen before any CI number existed, and those systems are
  not gates for #50.
- **The rate.** The backend's timer runs in whole milliseconds, so `perSecond` must divide 1000 (50 and 100
  do; 3 would run at 3.003); anything else is refused (`HANDLER_ERROR`, the subscription ends at once).
  After the last event the timer stops but the subscription stays open until the page unsubscribes (the
  benchmark does, when it has counted the events or waited for them).
- **Where it runs.** The self-test (ctest `hello-world.self-test`) runs in the CEF host jobs on linux-x64
  and linux-arm64 and on macOS and Windows. The `pd-info: bench …` lines are printed by
  `ctest --preset cef -R hello-world.self-test -V`, and `pnpm hello-world:self-test` on a machine with a
  host build. The statistics and the pass/fail rule are unit-tested (`bench.test.ts`), and the Pico side is
  unit-tested too (the backend publishes exactly `count` events of the size asked for and stops).
- **Not measured here.** The numbers below are from CI runs, not from the author's machine, which has no
  CEF host build. The slow-page case (a page that stops reading) is *not* part of this benchmark: the
  host bounds what it sends to a page that does not acknowledge (256 messages or 8 MiB, then new events
  are dropped and counted in `d`; docs/protocol.md, "Backpressure"), and `host.flood` (`tests/flood.sh`)
  tests that. A page that keeps up, as here, never reaches the bound.

### Results

From CI run 42576469621 (pull request #59), one run on a hosted runner each; the CEF host jobs print the
`pd-info: bench …` lines with `ctest --preset cef -R hello-world.self-test -V`. Latencies are in
milliseconds, p50 / p95 / p99 / max. **Nothing was lost and the publisher dropped nothing in any of the
ten runs.** "Arrived at" is the rate the page saw between its first and last event.

| System | Map tiles (30 000 B, 50/s): latency, arrived at | Telemetry (64 B, 100/s): latency, arrived at | Bridge round trip (64 B) p50 / p95 / p99 | 1000 concurrent calls |
|---|---|---|---|---|
| linux-x64 | 1 / 1 / 2 / 4, 49.9/s | 0 / 1 / 1 / 2, 99.3/s | 0.40 / 0.80 / 1.30 ms | 111 ms (9050/s) |
| linux-arm64 | 1 / 1 / 1 / 2, 49.8/s | 0 / 1 / 1 / 1, 99.3/s | 0.80 / 1.30 / 1.70 ms | 162 ms (6165/s) |
| macos-arm64 | 1 / 1 / 2 / 2, 45.7/s | 0 / 1 / 1 / 27, 81.7/s | 0.30 / 0.70 / 1.30 ms | 121 ms (8285/s) |
| windows-x64 | 1 / 1 / 1 / 2, 32.0/s | 0 / 1 / 1 / 1, 63.9/s | 0.70 / 1.00 / 1.20 ms | 316 ms (3162/s) |
| windows-arm64 | 1 / 2 / 2 / 2, 32.1/s | 1 / 1 / 1 / 6, 63.5/s | 0.90 / 1.50 / 1.70 ms | 297 ms (3364/s) |

What they say:

- **The path is fast and does not queue.** Both loads arrive within a few milliseconds on every system:
  the p95 is 1 to 2 ms against the gate's limit of 500 ms (about 250 times under it), and the largest
  maximum is 27 ms against 2 s (about 70 times). The bridge itself (a page's request answered by Pico)
  takes at most 1.7 ms at the 99th percentile.
- **The Linux gate has room.** The limits sit far above these values, so a failure there is more likely
  a regression than noise. This table cannot show the spread, though: it is one run per system. The CI
  history of the gate will, and the limits should be tightened only after seeing several runs. The one
  outlier is a 27 ms maximum on macOS telemetry.
- **The lower "arrived at" on macOS and Windows is probably not the path, but only Windows has a
  candidate explanation.** Nothing was lost and the latency is as low as on Linux, so the events were not
  held up in transit, and with a latency of 1 ms they were sent at about the rate they arrived. (The
  samples hold the send times, but the summary prints no send rate, so this is an inference.) On Windows (32/s of 50, 64/s of 100) a coarse
  timer tick would give this: the default resolution is about 15.6 ms, so a 20 ms interval would fire
  every 31 ms. That is my reading, not something the benchmark proves. **macOS (45.7/s of 50, 81.7/s of
  100) is not explained by it, and I have no explanation for it.** It does not matter for the gate
  (macOS and Windows only report), but until it is understood an app should not count on `setInterval`
  for an exact rate there.

**A page that stops reading (`host.flood`, docs/protocol.md "Backpressure").** The backend publishes a
counter every millisecond; the page does not read for two seconds, then reads for one and a half. The
host's bound on what a page has not acknowledged (256 messages or 8 MiB) turns the rest into `d`:

| System | received | dropped (`d`) | last event |
|---|---|---|---|
| linux-x64 | 1643 | 1595 | 3238 |
| linux-arm64 | 1677 | 1639 | 3316 |
| macos-arm64 | 1445 | 1287 | 2732 |

There is no Windows row: `host.flood` is not registered on Windows (`runtime/cef-host/CMakeLists.txt`
registers it only where `NOT WIN32`; it uses a shell script).

In each run, received plus dropped is exactly the last event number: nothing is unaccounted for. Most of
"received" is the events that arrived after the page started reading again. These numbers do not measure
the backlog kept during the pause: that it is at most 256 messages (or 8 MiB) is the host's bound,
checked by the router's unit tests (`runtime/cef-host/tests/router_test.cpp`), not read off this table.
