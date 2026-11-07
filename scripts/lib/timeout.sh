# shellcheck shell=bash
# `timeout SECONDS COMMAND [ARG...]` for the test scripts, sourced by each of them.
# macOS has no `timeout` unless coreutils is installed; there, `timeout` is pd_timeout_fallback,
# which behaves like coreutils' (the parts the scripts use): at the deadline the command gets
# SIGTERM, then SIGKILL if it's still running PD_TIMEOUT_KILL_AFTER seconds (default 5) later,
# and the exit is 124. Otherwise the command's own status (128+N when killed by signal N), 126
# when it can't be run and 127 when it isn't found. SIGTERM, SIGINT and SIGHUP sent to the
# fallback are passed on to the command.
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
