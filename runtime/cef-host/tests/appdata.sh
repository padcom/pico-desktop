#!/usr/bin/env bash
# The backend's data and cache directories, end to end: started by the host, a Pico backend gets
# $XDG_DATA_HOME/pico-desktop/apps/<app-id> and $XDG_CACHE_HOME/pico-desktop/apps/<app-id>/backend,
# created on first use and written to; and where they can't be (a file is in the way, or the
# directory is read-only) it reports an AppDirError naming the directory and the variable, and
# keeps running instead of crashing.
# Usage: appdata.sh <pico-desktop-host> <fixture dist>. Exit 77 without pico, a display or the fixture.
set -uo pipefail
host=$1 fixture=$2
here=$(cd "$(dirname "$0")" && pwd)
source "$here/lib.sh"
pd_require_pico
pd_require_fixture "$fixture"
# Re-run under Xvfb through bash, so the script doesn't depend on its executable bit.
pd_ensure_display bash "$0" "$@"
scratch=$(mktemp -d)
export XDG_CACHE_HOME=$scratch/cache
trap 'chmod -R u+w "$scratch" 2> /dev/null; rm -rf "$scratch"' EXIT
failed=0
fail() {
  echo "FAIL: $1"
  failed=1
}
wait_for() {  # file pattern seconds
  for _ in $(seq 1 $(( $3 * 4 ))); do grep -qF -- "$2" "$1" 2> /dev/null && return 0; sleep 0.25; done
  fail "no '$2' in $(basename "$1") within $3 s"
  return 1
}

pico build "$here/appdata-backend/main.pi" -o "$scratch/backend" > "$scratch/build.log" 2>&1 \
  || { cat "$scratch/build.log"; echo "FAIL: building the appdata backend"; exit 1; }

run() {  # name data-home: runs the host until the backend has said it is alive, then quits it
  mkfifo "$scratch/$1.in"
  exec 4<> "$scratch/$1.in"
  XDG_DATA_HOME=$2 "$host" --dev --control=stdin --no-external-open --app-dir="$fixture" --app-id=pd-appdata-test \
    --backend="$scratch/backend" < "$scratch/$1.in" > "$scratch/$1.log" 2>&1 &
  local pid=$!
  wait_for "$scratch/$1.log" 'appdata-backend: alive' 20
  echo quit >&4
  wait "$pid" || fail "the host exited with an error ($1)"
  exec 4>&-
}

# The normal case: neither directory exists yet.
data=$scratch/data
[[ -e $data ]] && fail "the data home exists before the first run"
run normal "$data"
app=$data/pico-desktop/apps/pd-appdata-test
grep -qF "appdata-backend: data $app" "$scratch/normal.log" || fail "the data directory isn't $app"
grep -qF "appdata-backend: cache $XDG_CACHE_HOME/pico-desktop/apps/pd-appdata-test/backend" "$scratch/normal.log" \
  || fail "the cache directory isn't under XDG_CACHE_HOME"
grep -qF 'appdata-backend: settings saved' "$scratch/normal.log" || fail "the backend couldn't write its settings"
[[ $(cat "$app/settings.json" 2> /dev/null) == saved ]] || fail "settings.json wasn't written to the data directory"
[[ $(cat "$XDG_CACHE_HOME/pico-desktop/apps/pd-appdata-test/backend/index" 2> /dev/null) == cached ]] \
  || fail "the cache file wasn't written to the cache directory"
[[ -e $app/.write-test ]] && fail "the write test left its file behind"

# A file where the data home should be: an error naming the directory and the variable, and a backend that carries on.
echo "not a directory" > "$scratch/blocker"
run blocked "$scratch/blocker"
grep -qF "appdata-backend: error NotWritable $scratch/blocker/pico-desktop/apps/pd-appdata-test" "$scratch/blocked.log" \
  || fail "no NotWritable error naming the directory"
grep -F 'appdata-backend: message' "$scratch/blocked.log" | grep -qF 'XDG_DATA_HOME' || fail "the error doesn't name XDG_DATA_HOME"
grep -qF 'appdata-backend: settings' "$scratch/blocked.log" && fail "the backend wrote settings where it couldn't"

# A read-only directory (as root nothing is read-only, so only the case above is checked then).
if [[ $(id -u) -ne 0 ]]; then
  mkdir -p "$scratch/ro/pico-desktop/apps/pd-appdata-test"
  chmod 555 "$scratch/ro/pico-desktop/apps/pd-appdata-test"
  run readonly "$scratch/ro"
  grep -qF "appdata-backend: error NotWritable $scratch/ro/pico-desktop/apps/pd-appdata-test" "$scratch/readonly.log" \
    || fail "no NotWritable error for the read-only directory"
  grep -F 'appdata-backend: message' "$scratch/readonly.log" | grep -qF 'permission denied' || fail "the error doesn't say why"
fi

if [[ $failed -ne 0 ]]; then
  tail -30 "$scratch"/*.log
  exit 1
fi
echo "ok: data and cache directories created and written; a blocked or read-only location is an error, not a crash"
