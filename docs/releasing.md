# Releasing Pico Desktop

A release publishes two things that are versioned separately ([versioning.md](versioning.md)):

- **the runtime** per target: `pico-desktop-host` with CEF, as `pico-desktop-runtime-<version>-<target>.tar.gz` (+ `.sha256`), attached to a GitHub release;
- **the packages**, one version for all: `@pico-desktop/protocol`, `@pico-desktop/communicator`, `@pico-desktop/communicator-pico`, `@pico-desktop/schema-tooling`, `@pico-desktop/vite-plugin` and the `pico-desktop` CLI, published to npm.

Nothing here is automated yet: there's no release workflow. The steps are manual, and each can be checked.

## Before

1. `main` is green in CI: JS, native, CEF host and Pico jobs.
2. Locally, on each target being released: `pnpm check`, then `pnpm cef`. The latter builds the host, stages the runtime and runs the 19 host ctests, including `hello-world.self-test`, `cli.dev-loop` and `cli.bundle`.
3. The Pico version the framework needs is released: `toolchain.json`'s `pico.minimum` (0.4.2), and it's what `runtime.json` records.
4. For 1.0 only: the six-target smoke matrix (PLAN.md, F7). Until then, releases are 0.x and linux-arm64 is the target that's checked.

## Versions

- **Runtime:** `project(pico_desktop_runtime VERSION x.y.z)` in `CMakeLists.txt`. Bump it when the host or CEF changes.
- **Packages:** every `package.json` under `packages/` and `tooling/` gets the same version, and so does `packages/communicator-pico/project.json`. The template refers to them through `{{dep:…}}` and `{{version:…}}` placeholders, so a created app picks up the new versions.
- **Protocol:** stays 1 unless a change breaks old peers (see versioning.md).

## The runtime

On each target machine:

```sh
pnpm cef                        # build the host, then stage and test the runtime
ls build/runtime/               # pico-desktop-runtime-<version>-<target>.tar.gz and .sha256
```

`scripts/stage-runtime.mjs` strips the shared libraries, hashes every file into `runtime.json`, and writes the archive and its checksum. The licenses (CEF, Chromium credits, Pico Desktop) are inside, under `licenses/`.

Create the GitHub release `runtime-v<version>` and attach each target's archive and `.sha256`. The CLI downloads a runtime by URL: an app sets `runtime` to it, or `$PICO_DESKTOP_RUNTIME` does. It checks the `.sha256` and then every file against `runtime.json`, and caches what it downloaded.

## The packages

Every package is `"private": true` today, so nothing is published by accident, and `pnpm publish`
skips private packages. The first release removes that flag on purpose: a reviewed change of its own.

```sh
pnpm build
pnpm -r publish --access public --dry-run   # check what each package contains
pnpm -r publish --access public
```

Publish in dependency order, which `pnpm -r` follows: protocol, schema-tooling, communicator, communicator-pico, vite-plugin, then the CLI. Each package carries `LICENSE`; a test keeps the copies identical to the repository's.

## After

- In a clean directory, with nothing linked:
  ```sh
  npx pico-desktop@<version> create demo
  cd demo
  pnpm install
  pnpm test
  pnpm exec pico-desktop build
  ```
  Run the bundle's launcher with a bare `PATH`, as `cli.bundle` does. It must start without Node, pnpm or Pico.
- Update the tutorial's version numbers and the Drone Configurator's dependencies.

## Not yet

- No release workflow builds the runtimes on native runners for the six targets (F6).
- The AppImage needs `appimagetool`. `pico-desktop package` makes the AppDir.
- Signing and notarization for macOS and Windows are deferred (F7).
