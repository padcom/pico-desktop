#!/usr/bin/env bash
# Unit tests for lib.sh, the helper the host's end-to-end scripts source. Each case runs in a fresh
# bash with an empty environment and a PATH of its own: real tools linked in, fakes (uname,
# xvfb-run, pico) written per case, and nothing else, so `timeout` and xvfb-run are absent unless
# a case adds them. No display or CEF needed.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
lib=$here/lib.sh
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
failed=0 cases=0

# A new bin dir with the real tools lib.sh and the cases use; prints its path.
new_bin() {
  local bin
  bin=$(mktemp -d "$scratch/bin.XXXX")
  for tool in sh grep uname perl sleep cat env; do
    local real
    real=$(command -v "$tool") || continue
    ln -s "$real" "$bin/$tool"
  done
  echo "$bin"
}
# Writes an executable $1/$2 whose body is $3 (sh), replacing the link to the real tool (never
# writing through it).
fake() {
  rm -f "$1/$2"
  printf '#!/bin/sh\n%s\n' "$3" > "$1/$2"
  chmod +x "$1/$2"
}
# Runs bash snippet $2 after sourcing lib.sh, PATH=$1, plus the environment assignments in $3...;
# sets out (stdout and stderr) and code.
run() {
  local bin=$1 snippet=$2
  shift 2
  out=$("$(command -v env)" -i PATH="$bin" "$@" "$BASH" -c "set -uo pipefail; source '$lib'; $snippet" 2>&1)
  code=$?
}
check() {  # name condition...
  local name=$1
  shift
  cases=$((cases + 1))
  if "$@"; then
    echo "ok   $name"
  else
    echo "FAIL $name (exit $code, output: ${out:-<none>})"
    failed=$((failed + 1))
  fi
}
is() { [[ "$1" == "$2" ]]; }

# --- pd_skip
bin=$(new_bin)
run "$bin" 'pd_skip "no widget"; echo after'
check "pd_skip prints the reason and exits 77" is "$code/$out" "77/skipped: no widget"

# --- pd_needs_display
fake "$bin" uname 'echo Linux'
run "$bin" 'pd_needs_display && echo needs || echo has'
check "Linux without DISPLAY or WAYLAND_DISPLAY needs a display" is "$out" needs
run "$bin" 'pd_needs_display && echo needs || echo has' DISPLAY=:9
check "DISPLAY is a display" is "$out" has
run "$bin" 'pd_needs_display && echo needs || echo has' WAYLAND_DISPLAY=wayland-0
check "WAYLAND_DISPLAY is a display" is "$out" has
run "$bin" 'pd_needs_display && echo needs || echo has' DISPLAY=
check "an empty DISPLAY isn't" is "$out" needs
darwin=$(new_bin)
fake "$darwin" uname 'echo Darwin'
run "$darwin" 'pd_needs_display && echo needs || echo has'
check "macOS never needs one" is "$out" has

# --- pd_display_runner
run "$bin" 'pd_display_runner; echo "${#pd_runner[@]}:${pd_runner[*]-}"' DISPLAY=:9
check "with a display, the runner is empty" is "$code/$out" "0/0:"
run "$bin" 'pd_display_runner; echo ran'
check "without a display or xvfb-run, it skips" is "$code/$out" "77/skipped: no display (set DISPLAY or install xvfb-run)"
xvfb=$(new_bin)
fake "$xvfb" uname 'echo Linux'
fake "$xvfb" xvfb-run 'for a in "$@"; do echo "[$a]"; done'
run "$xvfb" 'pd_display_runner; "${pd_runner[@]}" host --x'
check "without a display, the runner is a sized Xvfb" is "$code/$out" \
  "0/[--auto-servernum]"$'\n'"[--server-args=-screen 0 1280x1024x24]"$'\n'"[host]"$'\n'"[--x]"
run "$darwin" 'pd_display_runner; echo "${#pd_runner[@]}"; echo ${pd_runner[@]+"${pd_runner[@]}"} ran'
check "on macOS the runner is empty, and its guarded expansion is fine under set -u" is "$code/$out" "0/0"$'\n'"ran"

# --- pd_ensure_display
run "$bin" 'pd_ensure_display script arg; echo continued' DISPLAY=:9
check "with a display, the script continues" is "$code/$out" "0/continued"
run "$bin" 'pd_ensure_display script arg; echo continued'
check "without a display or xvfb-run, it skips" is "$code/$out" "77/skipped: no display (set DISPLAY or install xvfb-run)"
run "$xvfb" 'pd_ensure_display ./script.sh "a b"; echo continued'
check "without a display, it re-runs the script (arguments intact) under Xvfb" is "$code/$out" \
  "0/[--auto-servernum]"$'\n'"[--server-args=-screen 0 1280x1024x24]"$'\n'"[./script.sh]"$'\n'"[a b]"

# --- timeout
run "$bin" 'type -t timeout'
check "without timeout(1), lib.sh defines a timeout function" is "$out" function
if [[ -e "$bin/perl" ]]; then
  run "$bin" 'timeout 5 sh -c "exit 3"'
  check "the fallback runs the command and keeps its exit code" is "$code" 3
  SECONDS=0
  run "$bin" 'timeout 1 sleep 10'
  check "the fallback stops a command that runs too long" eval '[[ $code -ne 0 && $SECONDS -lt 5 ]]'
fi
withtimeout=$(new_bin)
ln -s "$(command -v timeout)" "$withtimeout/timeout"
run "$withtimeout" 'type -t timeout'
check "with timeout(1), it's left alone" is "$out" file

# --- pd_require_pico
run "$bin" 'pd_require_pico; echo found'
check "no pico on the PATH: skipped" is "$code/$out" "77/skipped: no Pico compiler on the PATH"
nano=$(new_bin)
fake "$nano" pico 'echo "GNU nano, version 7.2"'
run "$nano" 'pd_require_pico; echo found'
check "Debian's pico (nano) isn't the compiler" is "$code" 77
compiler=$(new_bin)
fake "$compiler" pico 'echo "pico 0.4.2"'
run "$compiler" 'pd_require_pico; echo found'
check "the Pico compiler is found" is "$code/$out" "0/found"

# --- pd_require_fixture
mkdir -p "$scratch/fixture"
run "$bin" "pd_require_fixture '$scratch/fixture'; echo found"
check "an unbuilt fixture: skipped" is "$code/$out" \
  "77/skipped: the fixture isn't built (pnpm --filter @pico-desktop/host-fixture build)"
touch "$scratch/fixture/index.html"
run "$bin" "pd_require_fixture '$scratch/fixture'; echo found"
check "a built fixture is found" is "$code/$out" "0/found"

echo "$cases cases, $failed failed"
[[ $failed -eq 0 ]]
