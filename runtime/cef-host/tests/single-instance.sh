#!/usr/bin/env bash
# Single instance: a second launch of an app (the same app ID) brings the first one's window to
# the front and exits 0; --multi-instance starts a separate instance with its own profile.
# Usage: single-instance.sh <pico-desktop-host> <fixture dist dir>. Exit 77 without a display or
# the fixture.
set -uo pipefail
host=$1 fixture=$2
source "$(dirname "$0")/lib.sh"
pd_require_fixture "$fixture"
pd_ensure_display "$0" "$@"
scratch=$(mktemp -d)
export XDG_CACHE_HOME=$scratch/cache  # never the user's profiles
first_pid=
cleanup() {
  [[ -n "$first_pid" ]] && kill "$first_pid" 2> /dev/null
  rm -rf "$scratch"
}
trap cleanup EXIT
failed=0
fail() {
  echo "FAIL: $1"
  failed=1
}
app=(--app-dir="$fixture" --app-id=pd-single-instance --no-external-open)

mkfifo "$scratch/control"
exec 3<> "$scratch/control"
"$host" "${app[@]}" --dev --control=stdin < "$scratch/control" > "$scratch/first.log" 2>&1 &
first_pid=$!
for _ in $(seq 1 40); do grep -q 'no --backend' "$scratch/first.log" && break; sleep 0.25; done
sleep 1

timeout 20 "$host" "${app[@]}" > "$scratch/second.log" 2>&1
code=$?
[[ $code -eq 0 ]] || fail "the second launch exited with $code"
grep -q 'is already running' "$scratch/second.log" || fail "the second launch didn't say the app is running"
for _ in $(seq 1 20); do grep -q 'launched again' "$scratch/first.log" && break; sleep 0.25; done
grep -q 'launched again: bringing the window to the front' "$scratch/first.log" || fail "the first instance wasn't told"

# --multi-instance: its own profile, so it runs (and quits at the end of its stdin).
timeout 30 "$host" "${app[@]}" --multi-instance --dev --control=stdin < /dev/null > "$scratch/third.log" 2>&1
grep -q 'no --backend' "$scratch/third.log" || fail "--multi-instance didn't start"
grep -q 'quitting' "$scratch/third.log" || fail "--multi-instance didn't quit at the end of its input"
[[ -z "$(ls -A "$XDG_CACHE_HOME/pico-desktop/apps/pd-single-instance/instances" 2> /dev/null)" ]] \
  || fail "--multi-instance left its profile behind"

echo quit >&3
for _ in $(seq 1 40); do kill -0 "$first_pid" 2> /dev/null || break; sleep 0.25; done
kill -0 "$first_pid" 2> /dev/null && fail "the first instance didn't quit"
first_pid=
sleep 1
pgrep -f -- "^$host " > /dev/null && { fail "processes outlived the test:"; pgrep -af -- "^$host "; }

grep -h '^pd-\|^pico-desktop-host:' "$scratch/first.log" "$scratch/second.log" "$scratch/third.log"
if [[ $failed -ne 0 ]]; then
  for f in first second third; do echo "---- $f ----"; cat "$scratch/$f.log"; done
fi
exit $failed
