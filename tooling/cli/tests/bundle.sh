#!/usr/bin/env bash
# The F6 bundle on linux-arm64, from a freshly created app:
#   build → run the bundle with a bare environment (PATH=/usr/bin:/bin: no Node, pnpm, Pico or
#   CMake) → SIGTERM closes it in order; package → the archive matches its .sha256 → unpacked
#   elsewhere it runs again; so does the AppDir's AppRun; a runtime with a file its manifest
#   doesn't list is refused.
# Usage: bundle.sh. Exit 77 without pico, a display, the built CLI or a staged runtime
# (`pnpm cef && pnpm runtime:stage` in the framework). With PD_BUNDLE_OUT set, the package (the
# .tar.gz and its .sha256) is copied there: CI runs it on a clean machine (smoke-bundle.sh).
set -uo pipefail
framework=$(cd "$(dirname "$0")/../../.." && pwd)
cli=$framework/tooling/cli/bin/pico-desktop.mjs
runtime=$(ls -d "$framework"/build/runtime/pico-desktop-runtime-*-linux-* 2> /dev/null | grep -v '\.tar\.gz' | tail -1)
# The Pico compiler, not Debian/Ubuntu's `pico` (nano).
pico --version 2> /dev/null | grep -q '^pico [0-9]' || { echo "skipped: no Pico compiler on the PATH"; exit 77; }
[[ -f "$framework/tooling/cli/dist/index.js" ]] || { echo "skipped: the CLI isn't built"; exit 77; }
[[ -n "$runtime" ]] || { echo "skipped: no staged runtime (pnpm runtime:stage)"; exit 77; }
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  command -v xvfb-run > /dev/null || { echo "skipped: no display"; exit 77; }
  exec xvfb-run --auto-servernum --server-args="-screen 0 1280x1024x24" "$0" "$@"
fi
scratch=$(mktemp -d)
trap 'pkill -KILL -f -- "$scratch" 2> /dev/null; rm -rf "$scratch"' EXIT
failed=0
fail() {
  echo "FAIL: $1"
  failed=1
}
# Runs a launcher with a bare environment until its backend is ready, then SIGTERMs it.
run_bundle() {
  local launcher=$1 log=$2
  env -i HOME="$HOME" DISPLAY="${DISPLAY:-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" XAUTHORITY="${XAUTHORITY:-}" \
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-}" \
    XDG_CACHE_HOME="$scratch/cache" PATH=/usr/bin:/bin \
    "$launcher" > "$log" 2>&1 &
  local pid=$!
  for _ in $(seq 1 80); do grep -q 'the backend is ready (epoch 1)' "$log" && break; sleep 0.25; done
  grep -q 'the backend is ready (epoch 1)' "$log" || fail "$(basename "$log"): the bundle's backend never got ready"
  kill -TERM "$pid"
  wait "$pid" || fail "$(basename "$log"): the bundle exited with $?"
  grep -q 'got signal 15: closing' "$log" || fail "$(basename "$log"): SIGTERM didn't close it in order"
  grep -q 'the backend ended (exit code 0)' "$log" || fail "$(basename "$log"): the backend didn't end cleanly"
}

for tool in node pnpm cmake; do
  if env -i PATH=/usr/bin:/bin sh -c "command -v $tool" > /dev/null; then
    echo "note: /usr/bin has $tool, so the bare environment doesn't prove the bundle works without it"
  fi
done
# (Debian's /usr/bin/pico is often nano; only the Pico compiler matters.)
if env -i PATH=/usr/bin:/bin sh -c 'pico --version 2> /dev/null' | grep -q '^pico [0-9]'; then
  echo "note: /usr/bin has the Pico compiler, so the bare environment doesn't prove the bundle works without it"
fi
# The staged runtime must have this checkout's host (it's copied into the bundle as it is).
host_build=$framework/build/cef/runtime/cef-host/pico-desktop-host
build_id() { readelf -n "$1" 2> /dev/null | awk '/Build ID/ { print $3 }'; }
if [[ -x "$host_build" && "$(build_id "$host_build")" != "$(build_id "$runtime/pico-desktop-host")" ]]; then
  echo "FAIL: $runtime has an older host than build/cef: run pnpm runtime:stage"
  exit 1
fi

cd "$scratch" || exit 1
node "$cli" create demo --id="dev.pico-desktop.bundle-$$" > /dev/null || { echo "FAIL: create"; exit 1; }
cd demo || exit 1
# From the cache when it can, from the registry what's missing: CI's cache has packages but not
# their metadata, and offline pnpm then skips optional platform packages (rolldown's native
# binding). The pnpm running is the one packageManager names, and pnpm 12 would resolve that pin
# as a config dependency: drop the pin.
node -e 'const f="package.json",p=JSON.parse(require("fs").readFileSync(f));delete p.packageManager;require("fs").writeFileSync(f,JSON.stringify(p,null,2)+"\n")'
pnpm install --prefer-offline > "$scratch/install.log" 2>&1 || { cat "$scratch/install.log"; echo "FAIL: install"; exit 1; }

PICO_DESKTOP_RUNTIME=$runtime node "$cli" build > "$scratch/build.log" 2>&1 || { cat "$scratch/build.log"; echo "FAIL: build"; exit 1; }
bundle=$(ls -d dist/demo-0.1.0-linux-*/ | head -1)
bundle=${bundle%/}
for f in demo manifest.json sbom.cdx.json THIRD-PARTY-NOTICES.txt app/index.html backend/backend runtime/pico-desktop-host runtime/runtime.json \
         runtime/licenses/CEF-LICENSE.txt runtime/licenses/CHROMIUM-CREDITS.html; do
  [[ -e "$bundle/$f" ]] || fail "the bundle has no $f"
done
node -e 'const b = require(process.argv[1]); const names = b.components.map(c => c.name);
  if (!names.includes("cef") || !names.includes("@pico-desktop/communicator")) process.exit(1)' "$PWD/$bundle/sbom.cdx.json" \
  || fail "the SBOM doesn't list CEF and the frontend's dependencies"
grep -q '^@pico-desktop/communicator ' "$bundle/THIRD-PARTY-NOTICES.txt" \
  || fail "THIRD-PARTY-NOTICES.txt doesn't list the frontend's dependencies"
run_bundle "$PWD/$bundle/demo" "$scratch/run.log"

PICO_DESKTOP_RUNTIME=$runtime node "$cli" package > "$scratch/package.log" 2>&1 || { cat "$scratch/package.log"; fail "package"; }
(cd dist && sha256sum -c "$(basename "$bundle").tar.gz.sha256" > /dev/null) || fail "the archive doesn't match its .sha256"
# The AppDir (the AppImage's contents): AppRun starts the app from it the same way.
for f in AppRun demo.desktop demo.svg .DirIcon; do
  [[ -e "$bundle.AppDir/$f" ]] || fail "the AppDir has no $f"
done
run_bundle "$PWD/$bundle.AppDir/AppRun" "$scratch/appdir.log"
if [[ -n "${PD_BUNDLE_OUT:-}" ]]; then
  mkdir -p "$PD_BUNDLE_OUT" && cp "$bundle.tar.gz" "$bundle.tar.gz.sha256" "$PD_BUNDLE_OUT/"
fi
mkdir -p "$scratch/elsewhere" && tar -xzf "$bundle.tar.gz" -C "$scratch/elsewhere"
run_bundle "$scratch/elsewhere/$(basename "$bundle")/demo" "$scratch/unpacked.log"

# A runtime with something its manifest doesn't list (a symlinked copy plus one file).
cp -rs "$runtime" "$scratch/tampered-runtime"
echo "not CEF" > "$scratch/tampered-runtime/libinjected.so"
PICO_DESKTOP_RUNTIME=$scratch/tampered-runtime node "$cli" build > "$scratch/tampered.log" 2>&1 && fail "a tampered runtime was used"
grep -q "has a file runtime.json doesn't list: libinjected.so" "$scratch/tampered.log" || { cat "$scratch/tampered.log"; fail "no word about the tampered runtime"; }

du -sh "$bundle" "$bundle.tar.gz" | sed 's/^/size: /'
grep -h 'pd-host: \(got signal\|the backend\)' "$scratch/run.log" "$scratch/unpacked.log" "$scratch/appdir.log"
if [[ $failed -ne 0 ]]; then
  for f in build run package unpacked; do echo "---- $f ----"; grep -v GetVSync "$scratch/$f.log" 2> /dev/null; done
fi
exit $failed
