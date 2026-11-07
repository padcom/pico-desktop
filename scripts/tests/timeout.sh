#!/usr/bin/env bash
# The test scripts' `timeout` fallback (runtime/cef-host/tests/lib.sh), run on every platform (#13): it
# exits 124 like coreutils' and stops commands that ignore SIGALRM or SIGTERM.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=../../runtime/cef-host/tests/lib.sh
source "$here/../../runtime/cef-host/tests/lib.sh"
failed=0
fail() {
  echo "FAIL: $1"
  failed=1
}
# expect <what> <status> <at most seconds> <timeout args...>
expect() {
  local what=$1 want=$2 within=$3
  shift 3
  local start=$SECONDS
  pd_timeout_fallback "$@" 2> /dev/null
  local got=$?
  local took=$((SECONDS - start))
  [[ $got == "$want" ]] || fail "$what: exit $got, want $want"
  ((took <= within)) || fail "$what: took ${took}s, want at most ${within}s"
}

expect "a command that finishes in time" 0 3 5 true
expect "the command's own exit status" 3 3 5 sh -c 'exit 3'
expect "arguments are passed as they are" 0 3 5 test "a b" = "a b"
expect "a missing command" 127 3 5 /nonexistent/command
expect "a command killed by a signal" 137 3 5 sh -c 'kill -KILL $$'
expect "a timeout exits 124" 124 4 1 sleep 30
expect "a command that ignores SIGALRM" 124 4 1 perl -e '$SIG{ALRM} = "IGNORE"; sleep 30'
PD_TIMEOUT_KILL_AFTER=1 expect "a command that ignores SIGTERM is killed" 124 5 1 \
  perl -e '$SIG{TERM} = "IGNORE"; sleep 30'

((failed == 0)) && echo "scripts.timeout: all checks passed"
exit "$failed"
