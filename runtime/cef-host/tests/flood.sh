#!/usr/bin/env bash
# A page that stops reading must not make the host's memory grow (docs/protocol.md §7): a backend
# publishes a counter every millisecond; the page (tests/flood-page) subscribes and then does not
# read for two seconds. The host must drop the new events beyond 256 unacknowledged ones and say
# so in `d`: what the page received plus what it was told was dropped adds up to the last counter.
# Usage: flood.sh <pico-desktop-host>. Exit 77 without pico or a display.
set -uo pipefail
host=$1
here=$(cd "$(dirname "$0")" && pwd)
source "$here/lib.sh"
pd_require_pico
pd_ensure_display "$0" "$@"
scratch=$(mktemp -d)
export XDG_CACHE_HOME=$scratch/cache
trap 'rm -rf "$scratch"' EXIT
failed=0
fail() {
  echo "FAIL: $1"
  failed=1
}
wait_for() {  # log pattern seconds
  for _ in $(seq 1 $(( $3 * 4 ))); do grep -qF -- "$2" "$1" 2> /dev/null && return 0; sleep 0.25; done
  fail "no '$2' in $(basename "$1") within $3 s"
  return 1
}

pico build "$here/flood-backend/main.pi" -o "$scratch/backend" > "$scratch/build.log" 2>&1 \
  || { cat "$scratch/build.log"; echo "FAIL: building the flood backend"; exit 1; }

mkfifo "$scratch/in"
exec 4<> "$scratch/in"
"$host" --dev --control=stdin --no-external-open --app-dir="$here/flood-page" --app-id=pd-flood \
  --backend="$scratch/backend" < "$scratch/in" > "$scratch/log" 2>&1 &
pid=$!
wait_for "$scratch/log" 'flood: subscribed' 20
wait_for "$scratch/log" 'flood: received=' 20
echo quit >&4
wait "$pid" || fail "the host exited with an error"
exec 4>&-

line=$(grep -o 'flood: received=.*' "$scratch/log" | head -1)
received=$(sed -n 's/.*received=\([0-9]*\).*/\1/p' <<< "$line")
dropped=$(sed -n 's/.*dropped=\([0-9]*\).*/\1/p' <<< "$line")
echo "$line"
[[ -n $dropped && $dropped -gt 0 ]] || fail "nothing was dropped: the host queued everything for the page"
[[ -n $received && $received -gt 0 ]] || fail "the page got no events after it started reading again"
grep -qF 'flood: accounted' "$scratch/log" || fail "received and dropped don't add up to the last event"
grep -qF "a page isn't keeping up" "$scratch/log" || fail "the host didn't say that it was dropping events"

if [[ $failed -ne 0 ]]; then
  grep -v GetVSync "$scratch/log" | tail -40
  exit 1
fi
echo "ok: a page that stopped reading was sent a bounded backlog, and told how many events it missed"
