#!/usr/bin/env bash
# Runs user-input.mjs (F12 and DevTools; a clicked external link) in a real window.
# Usage: user-input.sh <pico-desktop-host> <fixture dist dir>
# Exit 77 (ctest SKIP) without a display, the built fixture or the built CLI.
set -uo pipefail
host=$1 fixture=$2
here=$(cd "$(dirname "$0")" && pwd)
cli=$here/../../../tooling/cli/dist/index.js
[[ -f "$fixture/index.html" ]] || { echo "skipped: the fixture isn't built (pnpm --filter @pico-desktop/host-fixture build)"; exit 77; }
[[ -f "$cli" ]] || { echo "skipped: the CLI isn't built (pnpm build)"; exit 77; }
# macOS has no `timeout` unless coreutils is installed, and needs no X display.
command -v timeout > /dev/null || timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
if [[ "$(uname)" != Darwin && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  command -v xvfb-run > /dev/null || { echo "skipped: no display"; exit 77; }
  exec xvfb-run --auto-servernum --server-args="-screen 0 1280x1024x24" "$0" "$@"
fi
timeout 90 node "$here/user-input.mjs" "$host" "$fixture" "$cli"
