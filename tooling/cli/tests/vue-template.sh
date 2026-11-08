#!/usr/bin/env bash
# The Vue template (`pico-desktop create --template vue-ts`): create, install, unit and component
# tests, the vue-tsc typecheck, the production build (whose CSP test runs with the unit tests), and
# `pnpm e2e` in a real window, which fails on any CSP violation in the page's console.
# Usage: vue-template.sh <pico-desktop-host>. Exit 77 (ctest SKIP) without pico, a display or the host.
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
node "$cli" create demo --template vue-ts --id="dev.pico-desktop.vue-$$" > /dev/null || { echo "FAIL: create"; exit 1; }
cd demo || exit 1
# As in e2e.sh: the pnpm running is the one packageManager names, and pnpm 12 would resolve that
# pin as a config dependency.
node -e 'const f="package.json",p=JSON.parse(require("fs").readFileSync(f));delete p.packageManager;require("fs").writeFileSync(f,JSON.stringify(p,null,2))'
pnpm install --prefer-offline > "$scratch/install.log" 2>&1 || { cat "$scratch/install.log"; echo "FAIL: pnpm install"; exit 1; }
pnpm lint > "$scratch/lint.log" 2>&1 || { cat "$scratch/lint.log"; echo "FAIL: pnpm lint"; exit 1; }
pnpm test > "$scratch/test.log" 2>&1 || { cat "$scratch/test.log"; echo "FAIL: pnpm test"; exit 1; }
pnpm typecheck > "$scratch/typecheck.log" 2>&1 || { cat "$scratch/typecheck.log"; echo "FAIL: pnpm typecheck"; exit 1; }
pnpm e2e > "$scratch/e2e.log" 2>&1 || { cat "$scratch/e2e.log"; echo "FAIL: pnpm e2e"; exit 1; }
perl -pe 's/\e\[[0-9;]*m//g' "$scratch/e2e.log" | grep -q 'Tests  4 passed' \
  || { cat "$scratch/e2e.log"; echo "FAIL: pnpm e2e didn't run the template's 4 tests"; exit 1; }
echo "PASS: the Vue template tests, typechecks, builds and runs in a real window without CSP violations"
