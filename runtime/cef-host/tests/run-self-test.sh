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

# Windows: an ended run's profile (a pid no process has: Windows pids are multiples of 4) is
# removed by this run, which can't remove its own at exit (#23, #41). (2147483647: odd, and it fits
# the 32-bit long the sweep parses names into.)
stale=
case "$(uname)" in
  MINGW* | MSYS* | CYGWIN*)
    stale="$(cygpath -u "$LOCALAPPDATA")/pico-desktop/self-test/2147483647"
    mkdir -p "$stale/Default" && echo x > "$stale/Default/LOCK"
    ;;
esac

log=$(mktemp)
trap 'rm -f "$log"' EXIT
timeout 120 ${pd_runner[@]+"${pd_runner[@]}"} "$host" --app-dir="$fixture" --app-id=pd-self-test --origin="$origin" --self-test \
  --no-external-open > "$log" 2>&1
code=$?
grep -E '^(pd-|console: pd-info)' "$log"
# A failed self-test keeps its profile: show the end of CEF's log, often the only clue on CI.
kept=$(sed -n 's/^pico-desktop-host: the self-test failed; its CEF log is kept: //p' "$log" | tail -n 1 | tr -d '\r')
if [[ -n "$kept" ]]; then
  case "$(uname)" in MINGW* | MSYS* | CYGWIN*) kept=$(cygpath -u "$kept") ;; esac
  echo "---- $kept (last 60 lines) ----"
  tail -n 60 "$kept" 2> /dev/null || echo "(not readable)"
fi

failed=0
if [[ -n "$stale" && -e "$stale" ]]; then
  echo "FAIL: the ended self-test run's profile wasn't removed ($stale)"
  failed=1
fi
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
# (macOS: Chromium's processes are the bundle's helper apps.)
if pgrep -f -- "^$host " > /dev/null || pgrep -f -- "pico-desktop-host Helper" > /dev/null; then
  echo "FAIL: processes outlived the host:"
  pgrep -lf -- "pico-desktop-host"
  failed=1
fi

if [[ $failed -ne 0 ]]; then
  echo "---- full host output ----"
  cat "$log"
fi
exit $failed
