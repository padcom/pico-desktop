# Architecture: runtime responsibilities and boundaries

This is the F0 baseline: who owns what at runtime, and the interface between the CEF host and an
application. It turns PROJECT_DESCRIPTION.md §4–§6 into concrete component boundaries; the
decisions behind it are in [`adr/`](adr/README.md).

## Processes

```text
┌──────────────────────────── CEF renderer process (sandboxed) ───────────────────────────┐
│  Vite-built UI (TypeScript)  ──  @pico-desktop/communicator (TS)                         │
│                                   │ native bridge, injected only into the trusted origin │
└───────────────────────────────────┼──────────────────────────────────────────────────────┘
                                    │ CEF process messages (CBOR envelope bytes)
┌───────────────────────────────────┼──── pico-desktop-host (C++, CEF Views, browser process) ┐
│ ApplicationHost · WindowManager · ResourceManager · BridgeRouter · BackendSupervisor ·       │
│ NativeServices · SecurityPolicy                                                              │
└───────────────────────────────────┼──────────────────────────────────────────────────────────┘
                                    │ framed stdin/stdout pipes (ADR-0004)
┌───────────────────────────────────┼──── compiled Pico application process ─────────────────┐
│  communicator-pico  ──  registered RPC handlers, event emitters, subscription sources      │
│  domain services (OS, network, files, optional packages like pico-mavlink)                 │
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

CEF controls its own process topology (GPU, network, utility helpers); the only process Pico
Desktop adds is the Pico backend.

## Host components and their responsibilities

| Component | Owns | Never does |
|---|---|---|
| **ApplicationHost** | Startup and CEF initialization (sandbox on), the runtime manifest, the single-instance policy (default: single; second launch focuses the window), orderly shutdown | Application logic |
| **WindowManager** | CEF Views windows: one user-facing window in v1, but every window has a stable window ID and session routing so multi-window needs no protocol change | Decide what a window shows beyond the app's entry page |
| **ResourceManager** | Serving bundled assets under the trusted origin `https://<app-id>.localhost` (ADR-0003), path confinement (no traversal), MIME types, CSP headers | Run an HTTP listener in production |
| **BridgeRouter** | Injecting the bridge only into the trusted origin's allowed frames; routing envelopes renderer ↔ host ↔ backend by window, renderer context and backend epoch; frame size and queue limits; correlation of host-handled calls | Interpret application payloads beyond the envelope |
| **BackendSupervisor** | Launching the Pico process, readiness handshake, health checks, bounded restart with backoff and a budget, cooperative stop → grace period → forced kill, capturing stderr logs | Replay in-flight calls or reconstruct hardware state |
| **NativeServices** | Window controls, file dialogs, clipboard, notifications, lifecycle events, exposed as services (mainly to Pico), each behind a declared capability | Expose unrestricted primitives to JavaScript |
| **SecurityPolicy** | Origin/frame allowlist, navigation policy (external URLs open in the system browser), handler and capability permissions, DevTools only in development, no remote debugging in production, never silently disabling the sandbox | — |

## The application interface (what an app provides to the host)

A Pico Desktop application is data plus two programs; the host is the same binary for every app.

| Input | Form | Used by |
|---|---|---|
| App identity | `app.id` (reverse-DNS), name, version: from `pico-desktop.config.ts`, written into the runtime manifest at build time | ApplicationHost (single instance, origin), packaging |
| Frontend | Vite build output (`index.html` + assets) in the bundle's `app/` directory; in development the Vite dev-server URL instead | ResourceManager / WindowManager |
| Backend | The compiled Pico executable in the bundle, started with framed stdio (ADR-0004) | BackendSupervisor |
| Contract | The generated service registry (methods, events, subscriptions, the capabilities they need) from the schema (ADR-0005) | BridgeRouter (allowlist), both Communicators |
| Capabilities | Declared native services the app may use (dialogs, clipboard, notifications, …) | SecurityPolicy / NativeServices |
| Policy knobs | Window defaults, single-instance on/off, restart budget/backoff, shutdown grace period, message size/queue limits | the respective components |

The **runtime manifest** (written by `pico-desktop build`) records the app identity, the Pico
compiler version, the CEF revision, the host version, the protocol and contract versions and the
asset checksums (PROJECT_DESCRIPTION.md §8).

## Lifecycle (the states both Communicators report)

`connecting` → `ready` ⇄ (`disconnected` → `restarting` → `ready`) → `closing`, and `failed`
once the restart budget is spent. On a backend loss: pending calls reject with `DISCONNECTED`,
the window stays, the supervisor restarts the backend with backoff, managed subscriptions are
re-registered once `ready`, and nothing is replayed. Names are frozen with the protocol spec in
F2 (`CONNECTION_STATES` in `@pico-desktop/communicator`, `ConnectionState` in
`communicator-pico`).

## Repository map

| Path | What | Phase |
|---|---|---|
| `packages/protocol` | Envelope kinds, versions, framing constants (TS) | F2 |
| `packages/communicator-ts` | TypeScript Communicator | F3 |
| `packages/communicator-pico` | Pico Communicator (a Pico library: `pico test`) | F3 |
| `packages/schema-tooling` | Contract → TS/Pico types and validators | F2 |
| `runtime/cef-host` | `pico-desktop-host` (C++, CEF Views) | F1 |
| `runtime/ipc` | Native framing and transports (C++) | F2 |
| `tooling/cli` | The `pico-desktop` CLI | F4 |
| `tooling/vite-plugin` | Vite integration | F4/F6 |
| `tooling/templates` | `pico-desktop create` templates | F4 |
| `apps/hello-world` | The first end-to-end app | F3 |
| `tests/integration`, `tests/e2e` | Cross-process and developer-scenario tests | F3–F7 |
| `probes/` | Workstream 0 Pico capability probes | done |

## Boundaries the code must keep

- Domain logic stays out of the host (ADR-0001).
- Neither Communicator depends on CEF: both run over replaceable transports, including in-memory
  ones for Vitest and `pico test`.
- The framework doesn't depend on MAVLink, SerialPort or the Drone Configurator.
- Pico code uses actual Pico features: no async closures in 0.4 (pico-lang #52), so handlers are
  named functions or methods.
