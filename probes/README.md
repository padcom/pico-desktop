# Pico capability probes (Workstream 0)

Executable evidence for `../docs/pico-capabilities.md`. Audited on **pico 0.4.0**
(`padcom/ppl` @ `b5a9a05`). They now need a compiler with the pico-lang #82 fix (stdout flush):
0.4.2, or a local build of pico-lang `main` (`75fc4aa` or later).

```sh
pico test                                            # language, process, net, ffi probes
pico bench                                           # byte-copy cost
pico build --release src/stdio_echo.pi -o bin/stdio_echo
node harness/stdio-harness.mjs                       # framed binary stdio, parent side
```

| File | Covers |
|---|---|
| `src/basics.test.pi` | bytes, 64-bit ints, UTF-8, classes, callbacks, errors, async/timers/abort |
| `src/bytes.bench.pi` | `Uint8Array` copy/slice throughput |
| `src/stdio_echo.pi` + `harness/stdio-harness.mjs` | length-prefixed binary frames over stdin/stdout |
| `src/process.test.pi` | Pico as parent: spawn, pipes, exit codes, kill, NotFound, Pico↔Pico frames |
| `src/net.test.pi` | TCP echo / refused, UDP datagrams |
| `src/ffi.test.pi` | `extern "C"`, `c:` header import, termios on a pty |

`results/` holds the recorded harness output.
