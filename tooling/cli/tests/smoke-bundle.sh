#!/usr/bin/env bash
# A packaged app on a clean machine (F6): only what docs/platform-linux.md says a desktop has (the
# runtime libraries and a display), no build tree, no Node, pnpm, Pico or CMake. The archive
# matches its .sha256, nothing the host or the backend links is missing, and unpacked it starts
# with a bare environment, its backend gets ready, and SIGTERM closes it in order.
# Usage: smoke-bundle.sh <app>-<version>-<target>.tar.gz (its .sha256 beside it)
set -uo pipefail
archive=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  command -v xvfb-run > /dev/null || { echo "FAIL: no display and no xvfb-run"; exit 1; }
  exec xvfb-run --auto-servernum --server-args="-screen 0 1280x1024x24" "$0" "$@"
fi
scratch=$(mktemp -d)
trap 'pkill -KILL -f -- "$scratch" 2> /dev/null; rm -rf "$scratch"' EXIT
failed=0
fail() {
  echo "FAIL: $1"
  failed=1
}
for tool in node pnpm cmake; do
  env -i PATH=/usr/bin:/bin sh -c "command -v $tool" > /dev/null && echo "note: /usr/bin has $tool"
done

(cd "$(dirname "$archive")" && sha256sum -c "$(basename "$archive").sha256") || { echo "FAIL: the archive doesn't match its .sha256"; exit 1; }
tar -xzf "$archive" -C "$scratch" || { echo "FAIL: unpacking"; exit 1; }
bundle=$(ls -d "$scratch"/*/ | head -1)
bundle=${bundle%/}
# The launcher is the bundle's one top-level executable (named after the app).
launcher=$(find "$bundle" -maxdepth 1 -type f -perm -u+x | head -1)
echo "bundle: $(basename "$bundle"), launcher: $(basename "$launcher")"

for binary in "$bundle/runtime/pico-desktop-host" "$bundle/backend/backend"; do
  missing=$(LD_LIBRARY_PATH="$bundle/runtime" ldd "$binary" 2>&1 | grep 'not found')
  [[ -z "$missing" ]] || fail "$(basename "$binary") needs libraries this machine lacks: $missing"
done

log=$scratch/run.log
env -i HOME="$HOME" DISPLAY="${DISPLAY:-}" XAUTHORITY="${XAUTHORITY:-}" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" \
  XDG_CACHE_HOME="$scratch/cache" PATH=/usr/bin:/bin "$launcher" > "$log" 2>&1 &
pid=$!
for _ in $(seq 1 120); do grep -q 'the backend is ready (epoch 1)' "$log" && break; sleep 0.25; done
grep -q 'the backend is ready (epoch 1)' "$log" || fail "the backend never got ready"
kill -TERM "$pid"
wait "$pid" || fail "the app exited with $?"
grep -q 'got signal 15: closing' "$log" || fail "SIGTERM didn't close it in order"
grep -q 'the backend ended (exit code 0)' "$log" || fail "the backend didn't end cleanly"
sleep 1
pgrep -f -- "$scratch" > /dev/null && fail "processes outlived the app: $(pgrep -af -- "$scratch")"

grep 'pd-host: \(the backend\|got signal\|sandbox\)' "$log"
if [[ $failed -ne 0 ]]; then
  echo "---- the app's output ----"
  grep -v GetVSync "$log"
  exit 1
fi
echo "PASS: $(basename "$archive") runs on a clean machine"
