#!/usr/bin/env bash
# Host services from a Pico backend, end to end (ADR-0008): a desktop notification reaches the
# desktop's notify-send (a fake one here, recording its arguments) when the app declares the
# `notifications` capability, and is refused with NOT_ALLOWED when it doesn't.
# Usage: services.sh <pico-desktop-host> <fixture dist>. Exit 77 without pico, a display or the fixture.
set -uo pipefail
host=$1 fixture=$2
here=$(cd "$(dirname "$0")" && pwd)
pico --version 2> /dev/null | grep -q '^pico [0-9]' || { echo "skipped: no Pico compiler on the PATH"; exit 77; }
[[ -f "$fixture/index.html" ]] || { echo "skipped: the fixture isn't built"; exit 77; }
# macOS has no `timeout` unless coreutils is installed, and needs no X display.
command -v timeout > /dev/null || timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }
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
wait_for() {  # file pattern seconds
  for _ in $(seq 1 $(( $3 * 4 ))); do grep -qF -- "$2" "$1" 2> /dev/null && return 0; sleep 0.25; done
  fail "no '$2' in $(basename "$1") within $3 s"
  return 1
}

pico build "$here/services-backend/main.pi" -o "$scratch/backend" > "$scratch/build.log" 2>&1 \
  || { cat "$scratch/build.log"; echo "FAIL: building the services backend"; exit 1; }
# A fake notify-send, first on the PATH: it records its arguments, one per line.
mkdir -p "$scratch/bin"
printf '#!/bin/sh\nfor a in "$@"; do echo "$a"; done > "%s/notified"\n' "$scratch" > "$scratch/bin/notify-send"
chmod +x "$scratch/bin/notify-send"

run() {  # name capabilities expected-line
  mkfifo "$scratch/$1.in"
  exec 4<> "$scratch/$1.in"
  PATH="$scratch/bin:$PATH" "$host" --dev --control=stdin --no-external-open --app-dir="$fixture" --app-id="pd-services-$1" \
    --backend="$scratch/backend" $2 < "$scratch/$1.in" > "$scratch/$1.log" 2>&1 &
  local pid=$!
  wait_for "$scratch/$1.log" "$3" 15
  echo quit >&4
  wait "$pid" || fail "the host exited with an error ($1)"
  exec 4>&-
}

run allowed --capabilities=notifications 'services-backend: notify ok'
wait_for "$scratch/notified" 'Pico Desktop test' 5
grep -qxF 'from the backend' "$scratch/notified" || fail "the notification's body didn't reach notify-send"
rm -f "$scratch/notified"
run refused '' 'services-backend: notify NOT_ALLOWED'
[[ -e "$scratch/notified" ]] && fail "notify-send ran without the capability"

if [[ $failed -ne 0 ]]; then
  tail -20 "$scratch"/*.log
  exit 1
fi
echo "ok: a notification with the capability, NOT_ALLOWED without it"
