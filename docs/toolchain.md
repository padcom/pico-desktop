# Toolchain pins and binary provenance

The machine-readable pins are in [`../toolchain.json`](../toolchain.json) (plus `.nvmrc`,
`packageManager` in `package.json` and `pnpm-lock.yaml`). Change a pin deliberately, in its own
commit, saying why.

## Development tools

| Tool | Pin | Where it's pinned | How it's installed (primary dev host: Debian 13, linux-arm64) |
|---|---|---|---|
| Node.js | 26.9.0 | `.nvmrc`, `engines` | fnm/nvm (`nvm install`) |
| pnpm | 12.18.0 | `packageManager` | `npm install -g pnpm@12.18.0` (per Node version) |
| TypeScript | 5.9.3 | `package.json` | pnpm. Not 6/7 yet: `@padcom/vite-config-default` wants `<6`, typescript-eslint `<6.1` |
| ESLint, Vite, Vitest | 10.14.0, 8.3.6, 5.1.0 | `package.json` | pnpm; style from `@padcom/eslint-config-typescript` |
| CMake | ≥ 3.28 (developed with 4.4.5) | `CMakeLists.txt`, `CMakePresets.json` | Kitware's `cmake-4.4.5-linux-aarch64.tar.gz` in `~/.local/opt`, SHA-256 checked against `cmake-4.4.5-SHA-256.txt` from the GitHub release |
| C++ compiler | C++20 (developed with g++ 14.2.0) | `CMakeLists.txt` | distribution package |
| Pico | ≥ 0.4.2 | `toolchain.json` | see below |

### Pico

**Pico 0.4.2** (released 2026-11-07) has every fix the framework needs:
- stdout flushed to a pipe (#82) and bulk byte copies (#84);
- finalizers and buffer memory (#90, PR #91), and workspace imports from relative paths
  (#93, PR #94);
- buffers freed in long synchronous code (#95, PR #96), and `Map` keeping only live entries
  (#97, PR #98). `ipc.soak` needs those two: without them the backend peaks at 560 MB, over the
  test's 150 MB limit.

The absolute-path workaround for #93 is gone: the CLI and hello-world pass relative paths again.

Two fixes are still in review. Until they're merged and released, develop with the compiler
built from the `pico-lang` clone next to this repository, first on `PATH`:
`export PATH=~/projects/padcom/pico/pico-lang/target/release:$PATH`. That build comes from a
local branch, `local/integration`, which is never pushed: pico-lang `main` plus these fixes.
- `pico dap` attach with the formatters (#99, PR #101): merged on `main` (not released yet).
  `pnpm dev --debug` with VS Code's **Debug the backend** needs it.
- PR #103 (a subclass's fields after `instanceof` narrowing panicked the compiler, #102): only
  backends that narrow class hierarchies need it (pico-mavlink's decoded messages do).
- PR #105 (a field named `target` was taken for the build constant inside async methods, #104):
  pico-mavlink works around it with `this.target`.

pico-lang `main` also has `Reader.close()` (#100), which cancels a waiting read. padcom/serialport
uses it, so serial ports need a pico newer than 0.4.2 to close cleanly while a read is waiting.

Newer Pico is stricter than 0.4.1 in two ways the code handles: `TextDecoder`'s constructor
`throws RangeError` (#26), and later declarations in a block shadow earlier uses. CI's Pico job turns
on by itself once the 0.4.2 release exists (see `.github/workflows/ci.yml`). Release archives are
named `pico-<version>-<host>.tar.gz|zip` (linux-arm64, linux-x64, macos-arm64, windows-arm64,
windows-x64; no macos-x64, #86).

## Native runtime inputs

### CEF (provisional, F1 confirms)

- **Version:** `156.0.15+gcf671c0+chromium-156.0.8078.80` (Chromium 156.0.8078.80), the newest
  stable build published for all six targets as of 2026-11-06.
- **Source:** Spotify's CEF automated builds (`https://cef-builds.spotifycdn.com/`), listed in
  `index.json`; *minimal* distributions (no sample apps or debug symbols).
- **Integrity:** the index publishes SHA-1 per archive (recorded in `toolchain.json`).
  `scripts/fetch-cef.mjs` (`pnpm cef:fetch`) refuses an archive whose SHA-1 differs, and records
  its SHA-256; linux-arm64's (`806663a9…feb8`) is pinned in `toolchain.json`, and becomes the
  basis of the runtime archives Pico Desktop publishes (F6).
- **linux-arm64:** the prebuilt minimal archive (431 MB, 2.6 GB unpacked) works: the host
  builds against it and passes its self-tests (F1, `docs/platform-linux.md`).
- The CEF revision is pinned per Pico Desktop release and not updated opportunistically
  (PROJECT_DESCRIPTION.md §8).

### Published runtime archives (F6)

Each target's runtime archive will be listed in a versioned manifest with SHA-256 digests, the
compatible Pico/compiler version, the CEF revision and the protocol version; the CLI refuses
archives that don't match.
