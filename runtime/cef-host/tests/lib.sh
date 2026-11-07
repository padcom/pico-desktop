# Shared by the host's end-to-end test scripts (tests/*.sh here and apps/hello-world/scripts/
# self-test.sh): the skip convention, the prerequisite checks, the display and `timeout`. Source it,
# don't run it. Covered by tests/lib_test.sh (ctest host.test-lib).
#
# Exit code 77 is ctest's SKIP (SKIP_RETURN_CODE in CMakeLists.txt).

# The Xvfb a test runs in when there's no display: one sized screen, a free server number.
pd_xvfb=(xvfb-run --auto-servernum --server-args="-screen 0 1280x1024x24")

# `timeout SECONDS COMMAND [ARG...]`. macOS has no `timeout` unless coreutils is installed; there,
# `timeout` is pd_timeout_fallback, which behaves like coreutils' (the parts the scripts use, #13):
# at the deadline the command gets SIGTERM, then SIGKILL if it's still running
# PD_TIMEOUT_KILL_AFTER seconds (default 5) later, and the exit is 124. Otherwise the command's own
# status (128+N when killed by signal N), 126 when it can't be run and 127 when it isn't found.
# SIGTERM, SIGINT and SIGHUP sent to the fallback are passed on to the command. (Covered by
# scripts/tests/timeout.sh, ctest scripts.timeout.)
pd_timeout_fallback() {
  perl -e '
    use strict;
    use warnings;
    use POSIX qw(WNOHANG _exit);
    use Time::HiRes qw(time sleep);
    my $seconds = shift;
    my $grace = $ENV{PD_TIMEOUT_KILL_AFTER} // 5;
    my $pid = fork() // die "timeout: fork: $!\n";
    if ($pid == 0) {
      { exec { $ARGV[0] } @ARGV; }
      print STDERR "timeout: $ARGV[0]: $!\n";
      _exit($!{ENOENT} ? 127 : 126);
    }
    $SIG{$_} = sub { kill $_[0], $pid } for qw(TERM INT HUP);
    my $deadline = time + $seconds;
    my $timed_out = 0;
    while (waitpid($pid, WNOHANG) != $pid) {
      if (time >= $deadline) {
        kill($timed_out ? "KILL" : "TERM", $pid);
        $deadline = $timed_out ? 9**9**9 : time + $grace;
        $timed_out = 1;
      }
      sleep 0.05;
    }
    exit 124 if $timed_out;
    exit(($? & 127) ? 128 + ($? & 127) : $? >> 8);
  ' "$@"
}
command -v timeout > /dev/null || timeout() { pd_timeout_fallback "$@"; }

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
