#!/usr/bin/env bash
# A failed self-test keeps its profile, and with it CEF's log: the host exits 1, says where the log
# is, and the file is there (tests/failing-fixture fails one check). Then it's removed.
# Usage: keep-log.sh <pico-desktop-host>. Exit 77 without a display.
set -uo pipefail
host=$1
here=$(cd "$(dirname "$0")" && pwd)
source "$here/lib.sh"
pd_display_runner

log=$(mktemp)
trap 'rm -f "$log"' EXIT
timeout 120 ${pd_runner[@]+"${pd_runner[@]}"} "$host" --app-dir="$here/failing-fixture" --app-id=pd-keep-log \
  --self-test --no-external-open > "$log" 2>&1
code=$?

failed=0
fail() {
  echo "FAIL: $1"
  failed=1
}
[[ $code -eq 1 ]] || fail "the failing self-test exited with $code (expected 1)"
kept=$(sed -n 's/^pico-desktop-host: the self-test failed; its CEF log is kept: //p' "$log" | tail -n 1 | tr -d '\r')
[[ -n "$kept" ]] || fail "the host didn't say where the CEF log is kept"
if [[ -n "$kept" ]]; then
  case "$(uname)" in MINGW* | MSYS* | CYGWIN*) kept=$(cygpath -u "$kept") ;; esac
  [[ -f "$kept" ]] || fail "the kept CEF log isn't there ($kept)"
  # The kept profile is this test's to remove (the host has exited, so nothing holds it), but only
  # if the path is unmistakably a self-test profile: absolute, and named as cache_root names them
  # (POSIX: .../pd-host-self-test-XXXXXX; Windows: .../pico-desktop/self-test/<pid>). Anything
  # else is reported and left alone, never removed.
  profile=$(dirname "$kept")
  if [[ "$profile" == /* && ( "$(basename "$profile")" == pd-host-self-test-* ||
        "$profile" =~ /pico-desktop/self-test/[0-9]+$ ) && "$(basename "$kept")" == cef.log ]]; then
    rm -rf -- "$profile"
  else
    fail "the kept CEF log isn't in a self-test profile ($kept); not removing anything"
  fi
fi
if [[ $failed -ne 0 ]]; then
  echo "---- host output ----"
  cat "$log"
fi
exit "$failed"
