#!/usr/bin/env bash
# Hangs and crash loops (docs/protocol.md §7):
#   1. a backend whose event loop stops (tests/hang-backend: a busy loop after 1.5 s) is noticed by
#      the liveness pings (here every 300 ms, 1 s timeout), killed and restarted;
#   2. a backend that exits at once is restarted with backoff until the budget is spent, then the
#      page hears `failed` and nothing is started any more.
# Usage: hang.sh <pico-desktop-host> <fixture dist>. Exit 77 without pico, a display or the fixture.
set -uo pipefail
host=$1 fixture=$2
here=$(cd "$(dirname "$0")" && pwd)
command -v pico > /dev/null || { echo "skipped: no pico on the PATH"; exit 77; }
[[ -f "$fixture/index.html" ]] || { echo "skipped: the fixture isn't built"; exit 77; }
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
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
wait_for() {  # log pattern seconds
  for _ in $(seq 1 $(( $3 * 4 ))); do grep -qF -- "$2" "$1" && return 0; sleep 0.25; done
  fail "no '$2' in $(basename "$1") within $3 s"
  return 1
}
start() {  # name backend
  mkfifo "$scratch/$1.in"
  exec 4<> "$scratch/$1.in"
  "$host" --dev --control=stdin --no-external-open --liveness=300/1000 --app-dir="$fixture" --app-id="pd-hang-$1" \
    --backend="$2" < "$scratch/$1.in" > "$scratch/$1.log" 2>&1 &
  pid=$!
}
stop() {
  echo quit >&4
  wait "$pid" || fail "the host exited with an error ($1)"
  exec 4>&-
}

pico build "$here/hang-backend/main.pi" -o "$scratch/hang" > "$scratch/build.log" 2>&1 \
  || { cat "$scratch/build.log"; echo "FAIL: building the hang backend"; exit 1; }

# 1. A hang.
start hang "$scratch/hang"
wait_for "$scratch/hang.log" 'the backend is ready (epoch 1)' 15
wait_for "$scratch/hang.log" 'the backend stopped answering (no pong for' 10
wait_for "$scratch/hang.log" 'the backend exited (signal 9)' 5
wait_for "$scratch/hang.log" 'the backend is ready (epoch 2)' 10
stop hang

# 2. A crash loop: 5 restarts (250 ms … 4 s apart), then `failed`.
printf '#!/bin/sh\nexit 3\n' > "$scratch/crash" && chmod +x "$scratch/crash"
start crash "$scratch/crash"
wait_for "$scratch/crash.log" 'the backend keeps exiting; not starting it again' 20
starts=$(grep -c 'started the backend' "$scratch/crash.log")
[[ $starts -eq 6 ]] || fail "the crashing backend was started $starts times (expected 1 + 5 restarts)"
sleep 1
[[ $(grep -c 'started the backend' "$scratch/crash.log") -eq $starts ]] || fail "it was started again after failing"
stop crash

sleep 1
pgrep -f -- "^$host " > /dev/null && { fail "processes outlived the host"; pgrep -af -- "^$host "; }
pgrep -f -- "$scratch/hang" > /dev/null && fail "the hung backend outlived the host"
grep -h 'pd-host: \(the backend\|starting the backend again in\)' "$scratch/hang.log" "$scratch/crash.log" | sort | uniq -c
if [[ $failed -ne 0 ]]; then
  for f in hang crash; do echo "---- $f ----"; grep -v GetVSync "$scratch/$f.log"; done
fi
exit $failed
