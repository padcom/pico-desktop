#!/usr/bin/env bash
# Runs clipboard.mjs (the backend's clipboard against Chromium's) in a window of its own X server:
# the test replaces the clipboard's contents, so it never runs on the developer's display unless
# PD_TEST_REAL_CLIPBOARD=1 says it may.
# Usage: clipboard.sh <pico-desktop-host> <fixture dist dir>
# Exit 77 (ctest SKIP) without pico, xvfb-run (or the opt-in), the fixture or the built CLI.
set -uo pipefail
host=$1 fixture=$2
here=$(cd "$(dirname "$0")" && pwd)
cli=$here/../../../tooling/cli/dist/index.js
pico --version 2> /dev/null | grep -q '^pico [0-9]' || { echo "skipped: no Pico compiler on the PATH"; exit 77; }
[[ -f "$fixture/index.html" ]] || { echo "skipped: the fixture isn't built (pnpm --filter @pico-desktop/host-fixture build)"; exit 77; }
[[ -f "$cli" ]] || { echo "skipped: the CLI isn't built (pnpm build)"; exit 77; }
if [[ -z "${PD_CLIPBOARD_X:-}" ]]; then
  if command -v xvfb-run > /dev/null; then
    PD_CLIPBOARD_X=1 exec xvfb-run --auto-servernum --server-args="-screen 0 1280x1024x24" "$0" "$@"
  elif [[ "${PD_TEST_REAL_CLIPBOARD:-}" != 1 || ( -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ) ]]; then
    echo "skipped: no xvfb-run, and this test replaces the clipboard (PD_TEST_REAL_CLIPBOARD=1 runs it on this display)"
    exit 77
  fi
fi
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
pico build "$here/clipboard-backend/main.pi" -o "$scratch/backend" > "$scratch/build.log" 2>&1 \
  || { cat "$scratch/build.log"; echo "FAIL: building the clipboard backend"; exit 1; }
timeout 120 node "$here/clipboard.mjs" "$host" "$fixture" "$cli" "$scratch/backend"
