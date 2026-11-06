# ADR-0006: Six native targets; portable bundles; Linux AppImage

**Status:** Accepted (design rounds 1–8) · **Date:** 2026-11-06

## Decision
- Targets: `windows-x64`, `windows-arm64`, `macos-x64`, `macos-arm64`, `linux-x64`,
  `linux-arm64`. A target counts as supported only when packaged execution, native IPC, the
  sandbox, clean shutdown and dependency isolation pass on it, not when it merely compiles.
- `pico-desktop build` makes a portable runnable bundle; `pico-desktop package` packages it.
  **Linux AppImage (x64 and arm64) is required** for the first public release; other installers,
  signing and notarization are deferred.
- One CEF revision is pinned per Pico Desktop release (`toolchain.json`), with checksummed
  prebuilt runtime archives cached by the CLI.
- **Development priority is linux-arm64**; the other five targets gate the 1.0 release, not the
  current work.

## Current status of the inputs (2026-11-06)
- CEF 156.0.15 (Chromium 156.0.8078.80) has prebuilt *minimal* distributions for all six
  targets, including linux-arm64 (provisional pin in `toolchain.json`).
- Pico 0.4.0 is released for five of the six hosts; **macos-x64 has none** (pico-lang #86).
