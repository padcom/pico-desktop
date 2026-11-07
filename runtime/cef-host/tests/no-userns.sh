#!/usr/bin/env bash
# Without unprivileged user namespaces (and no setuid helper) the host refuses to start, saying
# why and how to enable them, and never starts Chromium unsandboxed. Simulated without root:
# inside a user namespace whose user.max_user_namespaces is 0, no nested one can be created.
# Usage: no-userns.sh <pico-desktop-host> <fixture dist>. Exit 77 where unshare can't do that.
set -uo pipefail
host=$1 fixture=$2
source "$(dirname "$0")/lib.sh"
pd_require_fixture "$fixture"
probe() {
  unshare --user --map-current-user --keep-caps sh -c 'echo 0 > /proc/sys/user/max_user_namespaces' 2> /dev/null
}
probe || pd_skip "this unshare can't make a namespace without user namespaces"
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
XDG_CACHE_HOME=$scratch timeout 60 unshare --user --map-current-user --keep-caps \
  sh -c 'echo 0 > /proc/sys/user/max_user_namespaces && exec "$0" --app-dir="$1" --self-test --no-external-open' \
  "$host" "$fixture" > "$scratch/out.log" 2>&1
code=$?
cat "$scratch/out.log"
failed=0
[[ $code -eq 1 ]] || { echo "FAIL: exit code $code (expected 1)"; failed=1; }
grep -q "doesn't let unprivileged processes create user namespaces" "$scratch/out.log" || { echo "FAIL: no explanation"; failed=1; }
grep -q 'never runs without its sandbox' "$scratch/out.log" || { echo "FAIL: no word about the sandbox"; failed=1; }
grep -q 'FATAL' "$scratch/out.log" && { echo "FAIL: Chromium started (and aborted)"; failed=1; }
exit $failed
