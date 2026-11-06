# Pico Desktop

Native desktop applications with a [Pico](https://github.com/padcom/pico-lang) backend and a
TypeScript + Vite UI, hosted by Chromium (CEF). The shipped app has no Node.js runtime: a C++ CEF
host runs the window, a compiled Pico process runs the application logic, and the two talk through
small, matching `Communicator` libraries over CBOR.

> **Status: phase F0 (repository skeleton).** Nothing here runs an app yet. See the plan and the
> architecture docs for what comes next.

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
| `docs/` | [Architecture](docs/architecture.md), [ADRs](docs/adr/README.md), [toolchain](docs/toolchain.md), [Pico capabilities](docs/pico-capabilities.md) |

## Development

Prerequisites (versions in [`toolchain.json`](toolchain.json)): Node.js from `.nvmrc`, pnpm,
CMake ≥ 3.28, a C++20 compiler, and Pico ≥ 0.4.2 (until it's released, a local
[pico-lang](https://github.com/padcom/pico-lang) build; see [docs/toolchain.md](docs/toolchain.md)).

```sh
nvm install                # Node from .nvmrc
npm install -g pnpm@12.18.0
pnpm install
pnpm check                 # lint, build, typecheck, test, native build + ctest, Pico tests
```

Individual steps: `pnpm lint`, `pnpm build`, `pnpm typecheck`, `pnpm test`, `pnpm native`,
`pnpm pico:test`.

## License

MIT for Pico Desktop's own code (see [LICENSE](LICENSE)); third-party components keep their
licenses.
