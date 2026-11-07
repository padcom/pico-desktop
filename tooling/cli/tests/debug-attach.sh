#!/usr/bin/env bash
# The Pico debugger on the backend that the host starts, the way the template's "Debug the
# backend" does it, on a freshly created app:
#   a debugger client (dap-attach.mjs, in VS Code's place) asks `pico dap` to attach to the next
#   backend that starts; `pnpm dev --debug` starts the app; the page's request stops the backend
#   at a breakpoint in its handler, where a variable is read (through pico's formatters); the
#   backend stays stopped for longer than the host's hang timeout and isn't restarted; the
#   debugger continues and detaches, and the page gets its answer from the same backend.
# Usage: debug-attach.sh <pico-desktop-host>. Exit 77 (ctest SKIP) without pico (with an `attach`
# in `pico dap`), lldb-dap, a ptrace scope that allows attaching, a display or the host.
set -uo pipefail
host=$1
framework=$(cd "$(dirname "$0")/../../.." && pwd)
cli=$framework/tooling/cli/bin/pico-desktop.mjs

pico --version 2> /dev/null | grep -q '^pico [0-9]' || { echo "skipped: no Pico compiler on the PATH"; exit 77; }
# `pico dap` attaches from 0.4.2 (pico-lang #99).
version=$(pico --version | cut -d' ' -f2)
[[ "$(printf '0.4.2\n%s\n' "$version" | sort -V | head -1)" == 0.4.2 ]] || { echo "skipped: pico $version's dap can't attach (0.4.2 can)"; exit 77; }
if [[ -z "${PICO_LLDB_DAP:-}" ]] && ! compgen -c lldb-dap | grep -q .; then
  echo "skipped: no lldb-dap"; exit 77
fi
# Yama lets a debugger attach only to its own children unless ptrace_scope is 0.
if [[ "$(cat /proc/sys/kernel/yama/ptrace_scope 2> /dev/null || echo 0)" != 0 ]]; then
  echo "skipped: kernel.yama.ptrace_scope isn't 0, so lldb can't attach"; exit 77
fi
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
dap_pid=
stop_dev() {
  printf '\003' >&3
}
cleanup() {
  [[ -n "$dap_pid" ]] && kill -KILL "$dap_pid" 2> /dev/null
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
# Waits up to $2 s (default 30) for a line matching $1 in file $3 (default the dev log).
wait_for() {
  for _ in $(seq 1 $((${2:-30} * 4))); do
    grep -qE -- "$1" "${3:-$log}" && return 0
    sleep 0.25
  done
  fail "no '$1' in ${3:-the dev output}"
  return 1
}

cd "$scratch" || exit 1
node "$cli" create demo --id="dev.pico-desktop.debug-$$" > /dev/null || { echo "FAIL: create"; exit 1; }
cd demo || exit 1
# As in dev-loop.sh: from the cache when it can, and without the packageManager pin.
node -e 'const f="package.json",p=JSON.parse(require("fs").readFileSync(f));delete p.packageManager;require("fs").writeFileSync(f,JSON.stringify(p,null,2)+"\n")'
pnpm install --prefer-offline > "$scratch/install.log" 2>&1 || { cat "$scratch/install.log"; echo "FAIL: pnpm install"; exit 1; }

# The page asks again and again, one request at a time (the first may come before the
# breakpoint is set), and logs each answer with the epoch: a backend that the host had restarted
# would be epoch 2.
cat >> apps/frontend/src/main.ts <<'TS'
let asking = false
setInterval(() => {
  if (app.communicator.state === 'ready' && !asking) {
    asking = true
    void app.communicator.request('greeting.hello', { name: 'debugger' }, { timeout: 120_000 })
      .then(({ message }) => console.log(`pd-debug: epoch ${app.communicator.epoch}: ${message}`))
      .finally(() => { asking = false })
  }
}, 1000)
TS
source=$PWD/apps/backend/app.pi
line=$(grep -n 'encodeGreeting(Greeting' "$source" | cut -d: -f1)
[[ -n "$line" ]] || { echo "FAIL: no greeting line in the template's backend"; exit 1; }

# The debugger first, as the launch configuration's comment says; it holds the backend stopped
# for 20 s, past the 15 s after which the host would otherwise count it as hung.
PD_DAP_HOLD_MS=20000 node "$framework/tooling/cli/tests/dap-attach.mjs" \
  "$PWD/.pico-desktop/dev/backend" "$source" "$line" request.name '"debugger"' > "$scratch/dap.log" 2>&1 &
dap_pid=$!
wait_for '^dap: attaching' 30 "$scratch/dap.log"

mkfifo "$scratch/keys"
exec 3<> "$scratch/keys"
PICO_DESKTOP_HOST=$host script -qfec "pnpm dev --debug" /dev/null < "$scratch/keys" > "$log" 2>&1 &
dev_pid=$!

wait_for 'dev │ debugging: the host no longer pings the backend'
wait_for '^dap: stopped at' 120 "$scratch/dap.log"
if wait "$dap_pid"; then
  dap_pid=
else
  dap_pid=
  fail "the debugger client"
fi
# An answer after the debugger detached: the stopped request went on (the page logs nothing
# while the backend is stopped, and the request it was answering times out only after 2 min).
before=$(grep -c 'pd-debug: epoch 1: Hello, debugger!' "$log")
wait_for 'pd-debug: epoch 1: Hello, debugger!'
sleep 2
[[ $(grep -c 'pd-debug: epoch 1: Hello, debugger!' "$log") -gt $before ]] || fail "no answer after the debugger detached"
grep -q 'pd-debug: epoch [2-9]' "$log" && fail "the backend was restarted"
grep -qE 'restarting the backend|the backend (hung|crashed)' "$log" && fail "the host restarted the stopped backend"

stop_dev
for _ in $(seq 1 40); do kill -0 "$dev_pid" 2> /dev/null || break; sleep 0.25; done
kill -0 "$dev_pid" 2> /dev/null && fail "pnpm dev didn't exit on Ctrl+C"
dev_pid=
grep -q 'the backend ended (exit code 0)' "$log" || fail "the backend didn't end cleanly after the debugger detached"
sleep 1
if pgrep -f -- "$scratch" > /dev/null || pgrep -f -- "^$host " > /dev/null; then
  fail "processes outlived pnpm dev:"
  pgrep -af -- "$scratch"
  pgrep -af -- "^$host "
fi

cat "$scratch/dap.log"
grep -E 'pd-debug|dev │ debugging' "$log"
if [[ $failed -ne 0 ]]; then
  echo "---- full dev output ----"
  cat "$log"
  exit 1
fi
echo "PASS: the debugger stopped the host's backend at a breakpoint, read a variable and detached"
