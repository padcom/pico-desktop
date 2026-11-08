# Versioning and compatibility

Three things are versioned separately, and each states what it works with.

| What | Version | Records what it works with in |
|---|---|---|
| **The protocol** ([protocol.md](protocol.md)) | an integer, `1` | every `hello` (`protocol: [min, max]`); `runtime.json` (`protocol`) |
| **The runtime** (`pico-desktop-host` + CEF, per target) | semver, from `CMakeLists.txt` | `runtime.json`: target, CEF and Chromium versions, protocol, the minimum Pico |
| **The packages** (CLI, Communicators, protocol, schema tooling) | semver, together | `package.json`; communicator-pico also in `project.json` |

Each app records the exact combination it was built with in its bundle's `manifest.json` (app, capabilities, runtime version, target, CEF, protocol, the Pico compiler) and in `sbom.cdx.json`.

## Rules

- **The protocol only grows within a version.** Receivers ignore keys they don't know, so new optional fields and new `lifecycle` names (such as `ping`/`pong`) stay in v1. Anything an old peer would misread needs version 2. Peers then agree on the highest version both speak, or refuse with `UNSUPPORTED_VERSION`.
- **A runtime serves one protocol range.** `build` refuses a runtime whose protocol isn't the one the CLI's apps speak.
- **The packages move together.** One version for the CLI, the TypeScript and Pico Communicators, the protocol package and the schema tooling: an app depends on matching ones.
- **Contracts are the app's.** Changing an app's own contract is between its frontend and backend, which are built together. Generated code is regenerated, never edited.
- **Pico:** `runtime.json` names the oldest compiler the framework works with (0.4.3). The CLI warns about older ones.

## Before 1.0

Everything is 0.x, and any 0.x release may change the public TypeScript and Pico APIs. Protocol v1 is frozen as documented, except for additions under the rules above. 1.0 freezes:
- the public APIs of `@pico-desktop/communicator` and `pico-desktop/communicator`;
- the CLI's commands and configuration;
- protocol v1;
- the bundle layout.

It ships once the six-target matrix passes (PLAN.md, F7).
