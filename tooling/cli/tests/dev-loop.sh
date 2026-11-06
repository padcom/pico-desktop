#!/usr/bin/env bash
# The F4 developer loop end to end, on a freshly created app:
#   create → pnpm install → pnpm test → pnpm dev, then in the same window:
#   3 backend edits (each rebuilt and restarted: epochs 2, 3, 4), a syntax error (reported, the
#   running backend kept), its fix (epoch 5), frontend edits (CSS hot-updated, a module reloads
#   the page), a contract edit (regenerated; the backend rebuilds: epoch 6), and Ctrl+C:
#   everything exits, nothing is left running.
# Usage: dev-loop.sh <pico-desktop-host>. Exit 77 (ctest SKIP) without pico, a display or the host.
set -uo pipefail
host=$1
framework=$(cd "$(dirname "$0")/../../.." && pwd)
cli=$framework/tooling/cli/bin/pico-desktop.mjs

command -v pico > /dev/null || { echo "skipped: no pico on the PATH"; exit 77; }
command -v script > /dev/null || { echo "skipped: no script (util-linux) for a pseudo-terminal"; exit 77; }
[[ -x "$host" ]] || { echo "skipped: $host isn't built"; exit 77; }
[[ -f "$framework/tooling/cli/dist/index.js" ]] || { echo "skipped: the CLI isn't built (pnpm build)"; exit 77; }
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  command -v xvfb-run > /dev/null || { echo "skipped: no display"; exit 77; }
  exec xvfb-run --auto-servernum --server-args="-screen 0 1280x1024x24" "$0" "$@"
fi

scratch=$(mktemp -d)
log=$scratch/dev.log
dev_pid=
# `pnpm dev` runs on a pseudo-terminal (`script`), and stopping it is typing Ctrl+C there: what
# a developer does, and the only way the signal reaches what pnpm runs.
stop_dev() {
  printf '\003' >&3
}
cleanup() {
  if [[ -n "$dev_pid" ]]; then
    stop_dev
    sleep 3
    kill -KILL "$dev_pid" 2> /dev/null
    pkill -KILL -f -- "$scratch" 2> /dev/null
  fi
  rm -rf "$scratch"
}
trap cleanup EXIT
failed=0
fail() {
  echo "FAIL: $1"
  failed=1
}
# Waits up to 30 s for a line matching $1 in the dev log.
wait_for() {
  for _ in $(seq 1 120); do
    grep -qE -- "$1" "$log" && return 0
    sleep 0.25
  done
  fail "no '$1' in the dev output"
  return 1
}

cd "$scratch" || exit 1
# A fresh app ID: a host still running elsewhere can't share this run's Chromium profile.
node "$cli" create demo --id="dev.pico-desktop.demo-$$" > /dev/null || { echo "FAIL: create"; exit 1; }
cd demo || exit 1
pnpm install --offline > "$scratch/install.log" 2>&1 || { cat "$scratch/install.log"; echo "FAIL: pnpm install"; exit 1; }
pnpm test > "$scratch/test.log" 2>&1 || { cat "$scratch/test.log"; fail "pnpm test"; }
(cd apps/frontend && pnpm exec tsc -p .) || fail "the template's frontend doesn't typecheck"

mkfifo "$scratch/keys"
exec 3<> "$scratch/keys"
PICO_DESKTOP_HOST=$host script -qfec "pnpm dev" /dev/null < "$scratch/keys" > "$log" 2>&1 &
dev_pid=$!
wait_for 'the backend is ready \(epoch 1\)'

for epoch in 2 3 4; do
  sed -i "s|message: \`[^\`]*|message: \`Hello #$epoch, \${request.name}!|" apps/backend/app.pi
  wait_for "the backend is ready \\(epoch $epoch\\)"
done

# A syntax error: reported, no restart; the epoch-4 backend keeps running.
printf '\nfunction broken( {\n' >> apps/backend/app.pi
wait_for 'pico │ .*error'
sleep 2
grep -q 'ready (epoch 5)' "$log" && fail "a broken build restarted the backend"
sed -i '/^function broken( {$/d' apps/backend/app.pi
wait_for 'the backend is ready \(epoch 5\)'

# The frontend, nothing else restarting: CSS is hot-updated; a module without an HMR boundary
# reloads the page (which reconnects to the same backend).
echo "h1 { color: rebeccapurple; }" >> apps/frontend/src/style.css
wait_for 'hmr update.*style\.css'
echo "// touched" >> apps/frontend/src/app.ts
wait_for 'page reload.*app\.ts'

# The contract: regenerated (the backend's generated module changes, so it rebuilds too).
sed -i 's|greeting = { message: tstr }|greeting = { message: tstr, ? polite: bool }|' packages/contracts/app.cddl
wait_for 'generated apps/frontend/src/generated/app.ts'
wait_for 'the backend is ready \(epoch 6\)'

stop_dev
for _ in $(seq 1 40); do kill -0 "$dev_pid" 2> /dev/null || break; sleep 0.25; done
kill -0 "$dev_pid" 2> /dev/null && fail "pnpm dev didn't exit on Ctrl+C"
dev_pid=
grep -q 'the backend ended (exit code 0)' "$log" || fail "the backend didn't end cleanly"
sleep 1
if pgrep -f -- "$scratch" > /dev/null || pgrep -f -- "^$host " > /dev/null; then
  fail "processes outlived pnpm dev:"
  pgrep -af -- "$scratch"
  pgrep -af -- "^$host "
fi

grep -E '^(dev|host) │ (dev │ |pd-host: )?' "$log" | grep -vE 'started the backend \(pid' | head -60
if [[ $failed -ne 0 ]]; then
  echo "---- full dev output ----"
  cat "$log"
fi
exit $failed
