# Shared by the host's end-to-end test scripts (tests/*.sh here and apps/hello-world/scripts/
# self-test.sh): the skip convention, the prerequisite checks, the display and `timeout`. Source it,
# don't run it. Covered by tests/lib_test.sh (ctest host.test-lib).
#
# Exit code 77 is ctest's SKIP (SKIP_RETURN_CODE in CMakeLists.txt).

# The Xvfb a test runs in when there's no display: one sized screen, a free server number.
pd_xvfb=(xvfb-run --auto-servernum --server-args="-screen 0 1280x1024x24")

# macOS has no `timeout` unless coreutils is installed: perl's alarm does the same (SIGALRM).
command -v timeout > /dev/null || timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }

# Skips the test: prints why and exits 77.
pd_skip() {
  echo "skipped: $*"
  exit 77
}

# Skips without the Pico compiler (not Debian/Ubuntu's `pico`, which is nano).
pd_require_pico() {
  pico --version 2> /dev/null | grep -q '^pico [0-9]' || pd_skip "no Pico compiler on the PATH"
}

# Skips unless the host fixture is built in dist dir $1.
pd_require_fixture() {
  [[ -f "$1/index.html" ]] || pd_skip "the fixture isn't built (pnpm --filter @pico-desktop/host-fixture build)"
}

# True where the host needs a display and has none. Only Linux needs one (an X or Wayland server).
pd_needs_display() {
  [[ "$(uname)" == Linux && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]
}

# Without a display, re-runs the whole calling script under Xvfb: `pd_ensure_display "$0" "$@"`.
# Skips when there's neither a display nor xvfb-run. Returns when there's a display.
pd_ensure_display() {
  pd_needs_display || return 0
  command -v xvfb-run > /dev/null || pd_skip "no display (set DISPLAY or install xvfb-run)"
  exec "${pd_xvfb[@]}" "$@"
}

# Sets the array pd_runner to the prefix that gives one command a display: empty when there is one,
# Xvfb otherwise. Skips when there's neither. Expand it as ${pd_runner[@]+"${pd_runner[@]}"}
# (bash 3.2, macOS's, calls an empty "${a[@]}" unbound under `set -u`).
pd_display_runner() {
  pd_runner=()
  pd_needs_display || return 0
  command -v xvfb-run > /dev/null || pd_skip "no display (set DISPLAY or install xvfb-run)"
  pd_runner=("${pd_xvfb[@]}")
}
