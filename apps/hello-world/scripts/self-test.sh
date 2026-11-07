#!/usr/bin/env bash
# The end-to-end self-test: the real host, the self-test build of the frontend and the compiled
# Pico backend, in a real window. The page drives the app (frontend/src/self-test.ts); this script
# checks what the page can't see: the host's log, the exit code, the backend's clean exit, and
# that no process outlives the host.
# Usage: self-test.sh <pico-desktop-host> (after `pnpm build:self-test && pnpm backend`).
# Exit 77 (ctest SKIP) without a display or the built artifacts.
set -uo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
host=$1 frontend=$here/dist/self-test backend=$here/dist/backend
source "$here/../../runtime/cef-host/tests/lib.sh"

for need in "$host" "$frontend/index.html" "$backend"; do
  if [[ ! -e "$need" ]]; then
    pd_skip "$need isn't built (pnpm --filter @pico-desktop/hello-world build:self-test, backend)"
  fi
done
pd_display_runner

log=$(mktemp)
trap 'rm -f "$log"' EXIT
timeout 120 ${pd_runner[@]+"${pd_runner[@]}"} "$host" --app-dir="$frontend" --app-id=dev.pico-desktop.hello-world \
  --backend="$backend" --capabilities=window --self-test --no-external-open > "$log" 2>&1
code=$?
grep -E '^(pd-|console: )' "$log"

failed=0
expect() {
  if ! grep -qF -- "$1" "$log"; then
    echo "FAIL: expected '$1' in the host's output"
    failed=1
  fi
}
[[ $code -eq 0 ]] || { echo "FAIL: the host exited with $code"; failed=1; }
expect "pd-test: done"
expect " 0 failed"
expect "pd-host: the backend is ready (epoch 1)"
expect "pd-host: the backend exited (signal 9)"
expect "pd-host: the backend is ready (epoch 2)"
# `closing` reached the backend and it ended by itself within the grace period.
expect "pd-host: the backend ended (exit code 0)"

sleep 1
if pgrep -f -- "^$host " > /dev/null || pgrep -fx -- "$backend" > /dev/null; then
  echo "FAIL: processes outlived the host:"
  pgrep -af -- "^$host "
  pgrep -afx -- "$backend"
  failed=1
fi

if [[ $failed -ne 0 ]]; then
  echo "---- full host output ----"
  cat "$log"
fi
exit $failed
