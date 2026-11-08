# Pico Desktop

Native desktop applications with a [Pico](https://github.com/padcom/pico-lang) backend and a
TypeScript + Vite UI, hosted by Chromium (CEF). The shipped app has no Node.js runtime: a C++ CEF
host runs the window, a compiled Pico process runs the application logic, and the two talk through
small, matching `Communicator` libraries over CBOR.

> **Status (2026-11-07): it works on linux-arm64.** Phases F0–F5 are done there (PLAN.md):
> - the CEF host with the sandbox on;
> - typed CBOR RPC, events and subscriptions between a Vite frontend and a compiled Pico backend;
> - the `pico-desktop` CLI with a live dev loop;
> - native services behind capabilities;
> - single instance, orderly close, crash and hang recovery.
>
> F6/F7 so far: portable bundles and archives (`pico-desktop build`/`package`). Still open: the AppImage, the other five targets, and published packages and runtimes. Start with the [tutorial](docs/tutorial.md).

## Layout

| Path | What |
|---|---|
| `packages/protocol` | Wire protocol constants (TS) |
| `packages/communicator-ts` | TypeScript Communicator |
| `packages/communicator-pico` | Pico Communicator (Pico library) |
| `packages/schema-tooling` | Contract → types and validators |
| `runtime/cef-host` | `pico-desktop-host`, the C++ CEF Views host |
| `runtime/ipc` | Native framing / transports (C++) |
| `tooling/cli` | The `pico-desktop` CLI |
| `tooling/vite-plugin`, `tooling/templates` | Vite integration, `create` templates |
| `apps/`, `tests/` | Example apps; integration and end-to-end tests |
| `probes/` | Pico capability probes (Workstream 0) |
| `docs/` | [Tutorial](docs/tutorial.md), [architecture](docs/architecture.md), [protocol](docs/protocol.md), [security model](docs/security-model.md), [troubleshooting](docs/troubleshooting.md), [versioning](docs/versioning.md), [ADRs](docs/adr/README.md), [toolchain](docs/toolchain.md), [Linux](docs/platform-linux.md), [Pico capabilities](docs/pico-capabilities.md) |

## Development

Prerequisites (versions in [`toolchain.json`](toolchain.json)): Node.js from `.nvmrc`, pnpm,
CMake ≥ 3.28, a C++20 compiler, and Pico ≥ 0.4.3 (a
[pico-lang](https://github.com/padcom/pico-lang) release; see [docs/toolchain.md](docs/toolchain.md)).

```sh
nvm install                # Node from .nvmrc
npm install -g pnpm@12.18.0
pnpm install
pnpm check                 # lint, build, typecheck, test, native build + ctest, Pico tests
```

Individual steps: `pnpm lint`, `pnpm build`, `pnpm typecheck`, `pnpm test`, `pnpm native`,
`pnpm pico:test`.

The CEF host (Linux for now, see [docs/platform-linux.md](docs/platform-linux.md)):

```sh
pnpm cef              # fetch + verify the pinned CEF, build the host, stage the runtime, run the ctests
build/cef/runtime/cef-host/pico-desktop-host --help
```

`pnpm cef` runs 16 ctests. Some need a display (Xvfb works) and `pico` on the `PATH`, and are skipped without them:
- the host's self-tests (assets, CSP, bridge, navigation, sandbox);
- single instance, orderly close, hang and crash-loop recovery, a missing sandbox;
- hello-world end to end;
- the CLI's dev loop and its bundle.

## Releasing

See [docs/releasing.md](docs/releasing.md).

## Related projects

These are independent projects: Pico Desktop doesn't depend on them.

- [pico-mavlink](https://github.com/padcom/pico-mavlink): MAVLink 1 and 2 for Pico.
- [pico-serialport](https://github.com/padcom/pico-serialport): serial ports for Pico.
- The Drone Configurator, a MAVLink configuration dashboard built with Pico Desktop on both of them. It's the reference application (`../drone-configurator` next to this checkout).

## License

MIT for Pico Desktop's own code (see [LICENSE](LICENSE)); third-party components keep their
licenses.
