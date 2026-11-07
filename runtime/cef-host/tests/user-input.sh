#!/usr/bin/env bash
# Runs user-input.mjs (F12 and DevTools; a clicked external link) in a real window.
# Usage: user-input.sh <pico-desktop-host> <fixture dist dir>
# Exit 77 (ctest SKIP) without a display, the built fixture or the built CLI.
set -uo pipefail
host=$1 fixture=$2
here=$(cd "$(dirname "$0")" && pwd)
source "$here/lib.sh"
cli=$here/../../../tooling/cli/dist/index.js
pd_require_fixture "$fixture"
[[ -f "$cli" ]] || pd_skip "the CLI isn't built (pnpm build)"
pd_ensure_display "$0" "$@"
timeout 90 node "$here/user-input.mjs" "$host" "$fixture" "$cli"
