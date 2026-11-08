#!/usr/bin/env bash
# `pnpm e2e` on a freshly created app, on any system. The host isn't named: the CLI has to find
# the framework's build by itself, and start the backend it built.
# Usage: e2e.sh <pico-desktop-host>. Exit 77 (ctest SKIP) without pico, a display or the host.
set -uo pipefail
host=$1
framework=$(cd "$(dirname "$0")/../../.." && pwd)
cli=$framework/tooling/cli/bin/pico-desktop.mjs

pico --version 2> /dev/null | grep -q '^pico [0-9]' || { echo "skipped: no Pico compiler on the PATH"; exit 77; }
[[ -e "$host" ]] || { echo "skipped: $host isn't built"; exit 77; }
[[ -f "$framework/tooling/cli/dist/index.js" ]] || { echo "skipped: the CLI isn't built (pnpm build)"; exit 77; }
if [[ "$(uname)" == Linux && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  command -v xvfb-run > /dev/null || { echo "skipped: no display"; exit 77; }
  exec xvfb-run --auto-servernum --server-args="-screen 0 1280x1024x24" "$0" "$@"
fi

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
unset PICO_DESKTOP_HOST

cd "$scratch" || exit 1
# A fresh app ID: a host still running elsewhere can't share this run's Chromium profile.
node "$cli" create demo --id="dev.pico-desktop.e2e-$$" > /dev/null || { echo "FAIL: create"; exit 1; }
cd demo || exit 1
# As in dev-loop.sh: the pnpm running is the one packageManager names, and pnpm 12 would resolve
# that pin as a config dependency.
node -e 'const f="package.json",p=JSON.parse(require("fs").readFileSync(f));delete p.packageManager;require("fs").writeFileSync(f,JSON.stringify(p,null,2))'
pnpm install --prefer-offline > "$scratch/install.log" 2>&1 || { cat "$scratch/install.log"; echo "FAIL: pnpm install"; exit 1; }
pnpm e2e > "$scratch/e2e.log" 2>&1 || { cat "$scratch/e2e.log"; echo "FAIL: pnpm e2e"; exit 1; }
# (Without colours: CI forces them. perl, because BSD sed doesn't know \x1b.)
perl -pe 's/\e\[[0-9;]*m//g' "$scratch/e2e.log" | grep -q 'Tests  3 passed' \
  || { cat "$scratch/e2e.log"; echo "FAIL: pnpm e2e didn't run the template's 3 tests"; exit 1; }
echo "PASS: pnpm e2e ran the template's 3 tests in a real window"
