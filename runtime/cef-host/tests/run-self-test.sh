#!/usr/bin/env bash
# Runs the host's self-test in a real window and checks what the page can't see from inside:
# the host's policy log lines, the result, and that no Chromium process outlives the host.
# Usage: run-self-test.sh <pico-desktop-host> <fixture dist dir> <app|https>
# Exit 77 (ctest SKIP) without a display or a built fixture.
set -uo pipefail
host=$1 fixture=$2 origin=$3
source "$(dirname "$0")/lib.sh"

pd_require_fixture "$fixture"
pd_display_runner

log=$(mktemp)
trap 'rm -f "$log"' EXIT
timeout 120 ${pd_runner[@]+"${pd_runner[@]}"} "$host" --app-dir="$fixture" --app-id=pd-self-test --origin="$origin" --self-test \
  --no-external-open > "$log" 2>&1
code=$?
grep -E '^(pd-|console: pd-info)' "$log"

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
expect "pd-test: ok renderer is sandboxed"
expect "pd-host: blocked navigation to https://example.com/ (no user gesture)"

# Nothing may outlive the host (renderers, GPU, zygotes, utilities).
sleep 2
if pgrep -f -- "^$host " > /dev/null; then
  echo "FAIL: processes outlived the host:"
  pgrep -af -- "^$host "
  failed=1
fi

if [[ $failed -ne 0 ]]; then
  echo "---- full host output ----"
  cat "$log"
fi
exit $failed
