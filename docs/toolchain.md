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

The framework needs the stdout-flush fix (pico-lang #82); the probes and the codec also use the
bulk byte copies (#84). Both are fixed in `padcom/pico-lang` but not released: **0.4.2** will
carry them (both are on pico-lang `main` since `75fc4aa`; the released 0.4.1 predates them). Until
then, develop with the compiler built from the `pico-lang` clone next to this repository
(`~/projects/padcom/pico/pico-lang`, `cargo build --release -p pico-cli`), first on `PATH`:
`export PATH=~/projects/padcom/pico/pico-lang/target/release:$PATH`. Since 2026-11-07 that
build comes from a local branch, `local/integration`, which is never pushed. It holds pico-lang
`main` (which has PR #91 since 2026-11-07: finalizers and buffer memory, #90) plus the fixes
still in review: PR #94 (workspace imports from relative paths, #93) and PR #96 (buffers freed in
long synchronous code, #95). The framework works around #93 with absolute paths
until #94 lands. Newer `main` is stricter than 0.4.1 in two ways the code now handles:
`TextDecoder`'s constructor `throws RangeError` (#26), and later declarations in a block shadow
earlier uses. CI's Pico job turns
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
