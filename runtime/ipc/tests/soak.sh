#!/usr/bin/env bash
# Builds the Pico echo backend and soaks it (soak.mjs). Exit 77 (ctest SKIP) without the Pico
# compiler, Node or the built protocol package.
set -uo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
pico --version 2> /dev/null | grep -q '^pico [0-9]' || { echo "skipped: no Pico compiler on the PATH"; exit 77; }
command -v node > /dev/null || { echo "skipped: no node"; exit 77; }
[[ -f "$root/packages/protocol/dist/index.js" ]] || { echo "skipped: the protocol package isn't built (pnpm build)"; exit 77; }
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
pico build --release "$root/packages/communicator-pico/bench/echo.pi" -o "$out/echo" > "$out/build.log" 2>&1 \
  || { cat "$out/build.log"; echo "FAIL: building the echo backend"; exit 1; }
node "$root/runtime/ipc/tests/soak.mjs" "$out/echo" "$@"
