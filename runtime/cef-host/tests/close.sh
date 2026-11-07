#!/usr/bin/env bash
# The orderly close: asked to close, the host first tells the page and the backend `closing`,
# keeps the window while the backend finishes, and closes once it has exited. A backend that
# doesn't finish is killed after the grace period (3 s). The UI thread never waits.
# Usage: close.sh <pico-desktop-host> <fixture dist> <hello-world dir>. Exit 77 without a display
# or the builds.
set -uo pipefail
host=$1 fixture=$2 hello=$3
[[ -f "$fixture/index.html" && -f "$hello/dist/frontend/index.html" && -x "$hello/dist/backend" ]] \
  || { echo "skipped: needs the fixture and hello-world (build, backend) built"; exit 77; }
# `timeout` (with a fallback where coreutils is missing, as on macOS); macOS needs no X display.
source "$(dirname "$0")/../../../scripts/lib/timeout.sh"
if [[ "$(uname)" != Darwin && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  command -v xvfb-run > /dev/null || { echo "skipped: no display"; exit 77; }
  exec xvfb-run --auto-servernum --server-args="-screen 0 1280x1024x24" "$0" "$@"
fi
scratch=$(mktemp -d)
export XDG_CACHE_HOME=$scratch/cache
trap 'rm -rf "$scratch"' EXIT
failed=0
fail() {
  echo "FAIL: $1"
  failed=1
}
# Runs the host until `wait_line` appears, then sends quit; the output is in $scratch/$name.log.
run() {
  local name=$1 wait_line=$2
  shift 2
  mkfifo "$scratch/$name.in"
  exec 4<> "$scratch/$name.in"
  "$host" --dev --control=stdin --trace-ipc --no-external-open "$@" < "$scratch/$name.in" > "$scratch/$name.log" 2>&1 &
  local pid=$!
  for _ in $(seq 1 60); do grep -q -- "$wait_line" "$scratch/$name.log" && break; sleep 0.25; done
  sleep 0.5
  local asked=$(date +%s%N)
  echo quit >&4
  wait "$pid"
  local code=$?
  exec 4>&-
  echo "$(( ($(date +%s%N) - asked) / 1000000 ))" > "$scratch/$name.ms"
  return $code
}
# The line numbers of the first matches of $2… in log $1, which must be in that order.
in_order() {
  local log=$1 last=0 line
  shift
  for pattern in "$@"; do
    line=$(grep -n -m1 -F -- "$pattern" "$log" | cut -d: -f1)
    if [[ -z "$line" || "$line" -le "$last" ]]; then
      fail "'$pattern' missing or out of order in $(basename "$log")"
      return
    fi
    last=$line
  done
}

# 1. A cooperative backend (hello-world): it ends by itself, after the page heard `closing`.
run cooperative 'ready (epoch 1)' --app-dir="$hello/dist/frontend" --app-id=pd-close-a --backend="$hello/dist/backend" \
  || fail "the host exited with an error (cooperative)"
in_order "$scratch/cooperative.log" 'closing: telling the page and the backend' 'host → backend lifecycle closing' \
  'host → page    lifecycle closing' 'the backend ended (exit code 0)'
grep -q "didn't finish in time" "$scratch/cooperative.log" && fail "the cooperative backend was killed"

# 2. A backend that never finishes: killed after the grace period, then the window closes.
printf '#!/bin/sh\nexec sleep 1000\n' > "$scratch/stuck" && chmod +x "$scratch/stuck"
run stuck 'started the backend' --app-dir="$fixture" --app-id=pd-close-b --backend="$scratch/stuck" \
  || fail "the host exited with an error (stuck)"
in_order "$scratch/stuck.log" 'closing: telling the page and the backend' "didn't finish in time; killing it" \
  'the backend ended (signal 9)'
ms=$(cat "$scratch/stuck.ms")
(( ms >= 2500 && ms < 8000 )) || fail "the stuck backend's close took ${ms} ms (expected about 3 s)"

sleep 1
pgrep -f -- "^$host " > /dev/null && { fail "processes outlived the host"; pgrep -af -- "^$host "; }
pgrep -f -- "$scratch/stuck" > /dev/null && fail "the stuck backend outlived the host"
grep -h 'pd-host: \(closing\|the backend ended\|the backend didn\)' "$scratch"/*.log
echo "stuck backend: closed after ${ms} ms"
if [[ $failed -ne 0 ]]; then
  for f in cooperative stuck; do echo "---- $f ----"; cat "$scratch/$f.log"; done
fi
exit $failed
