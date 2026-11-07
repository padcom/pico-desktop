#!/usr/bin/env bash
# Runs clipboard.mjs (the backend's clipboard against Chromium's) in a window of its own X server:
# the test replaces the clipboard's contents, so it never runs on the developer's display unless
# PD_TEST_REAL_CLIPBOARD=1 says it may.
# Usage: clipboard.sh <pico-desktop-host> <fixture dist dir>
# Exit 77 (ctest SKIP) without pico, xvfb-run (or the opt-in), the fixture or the built CLI.
set -uo pipefail
host=$1 fixture=$2
here=$(cd "$(dirname "$0")" && pwd)
source "$here/lib.sh"
cli=$here/../../../tooling/cli/dist/index.js
pd_require_pico
pd_require_fixture "$fixture"
[[ -f "$cli" ]] || pd_skip "the CLI isn't built (pnpm build)"
if [[ -z "${PD_CLIPBOARD_X:-}" ]]; then
  if command -v xvfb-run > /dev/null; then
    PD_CLIPBOARD_X=1 exec "${pd_xvfb[@]}" "$0" "$@"
  elif [[ "${PD_TEST_REAL_CLIPBOARD:-}" != 1 ]] || pd_needs_display; then
    pd_skip "no xvfb-run, and this test replaces the clipboard (PD_TEST_REAL_CLIPBOARD=1 runs it on this display)"
  fi
fi
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
pico build "$here/clipboard-backend/main.pi" -o "$scratch/backend" > "$scratch/build.log" 2>&1 \
  || { cat "$scratch/build.log"; echo "FAIL: building the clipboard backend"; exit 1; }
timeout 120 node "$here/clipboard.mjs" "$host" "$fixture" "$cli" "$scratch/backend"
