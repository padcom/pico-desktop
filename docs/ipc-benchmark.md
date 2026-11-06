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

| failure | observed (pipes) |
|---|---|
| backend killed (SIGKILL) with a request pending | EOF on the pipe after **0.07–0.14 ms** |
| backend hung (SIGSTOP) | the link stays open and silent: **only a timeout notices** |
| clean shutdown (close stdin, wait for exit) | **0.3–0.4 ms** |

With pipes, a crash is detected immediately; a hang needs the request timeouts and health checks of
F5. Host-side cost was negligible (0.8 s CPU and 8 MB RSS for the whole run, against the backend's 6.5 s).

## What the benchmark found in Pico: pico-lang #90

Backend memory grew with the number of messages (peak RSS **780 MB** for an echo), and 100-in-flight
throughput fell from ~100k msg/s in short runs to ~26k in long ones. In 0.4.1:
- the event loop runs finalization callbacks only once it's idle, which a backend never is, so `ArrayBuffer` memory was never freed;
- collections are triggered by GC-heap allocation only, which leaves off-heap buffer memory out of the picture.

pico-lang #90 has the analysis. A fix is prepared, but not merged yet: it runs due finalizers between busy loop steps and counts buffer memory toward the triggers. With it:
- a tight allocation loop stays at ~120 MB instead of growing past 660 MB;
- 100-in-flight throughput stays at ~50k msg/s;
- peak RSS is still ~420 MB, mostly from the 1 MiB phase, so more work is needed there (tracked in #90).

This doesn't depend on the transport: both transports showed the same growth.
