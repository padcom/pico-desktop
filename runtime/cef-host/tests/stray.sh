#!/usr/bin/env bash
# A backend that exits leaving a child that holds its pipes (tests/stray-backend): the host still
# sees the exit and restarts it (#17), and quitting doesn't wait on the pipes (#16). Like part 3
# of hang.sh, but the backend is a program and nothing needs pico or a fifo, so Windows runs it.
# Usage: stray.sh <pico-desktop-host> <fixture dist> <stray-backend>. Exit 77 without a display
# or the fixture.
set -uo pipefail
host=$1 fixture=$2 backend=$3
source "$(dirname "$0")/lib.sh"
pd_require_fixture "$fixture"
pd_ensure_display "$0" "$@"
scratch=$(mktemp -d)
export XDG_CACHE_HOME=$scratch/cache
trap 'rm -rf "$scratch" 2> /dev/null' EXIT
log=$scratch/host.log
: > "$log"
failed=0
fail() {
  echo "FAIL: $1"
  failed=1
}

# The strays hold the pipes for 30 s, so EOF comes only then: the exit must be seen well before
# (within 10 s of the backend's start) and quitting must not wait for it (all in under 25 s).
# Quit once the restart is announced, or 10 s after the start.
start=$SECONDS
{
  for _ in $(seq 1 120); do
    grep -qF 'started the backend' "$log" && break
    sleep 0.25
  done
  for _ in $(seq 1 40); do
    grep -qF 'starting the backend again in' "$log" && break
    sleep 0.25
  done
  echo quit
} | timeout 60 "$host" --dev --control=stdin --no-external-open --app-dir="$fixture" --app-id=pd-stray \
  --backend="$backend" > "$log" 2>&1
code=$?
took=$((SECONDS - start))

((code == 124)) && fail "the host didn't exit within 60 s"
((code == 0 || code == 124)) || fail "the host exited with $code"
grep -qF 'the backend exited (exit code 3)' "$log" || fail "the backend's exit wasn't noticed while the stray held its pipes"
grep -qF 'starting the backend again in' "$log" || fail "the backend wasn't restarted"
((took < 25)) || fail "quitting took ${took}s: the host waited on the stray's pipes"
((failed == 0)) || cat "$log"
# The strays, best effort.
case "$(uname)" in
  MINGW* | MSYS* | CYGWIN*) taskkill //F //IM "$(basename "$backend")" > /dev/null 2>&1 ;;
  *) pkill -f -- "$backend --hold" 2> /dev/null ;;
esac
exit "$failed"
