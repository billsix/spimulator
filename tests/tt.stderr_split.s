# SPIM S20 MIPS simulator.
# Test that program output goes to stdout and spim's
# informational messages ("Loaded: ...", undefined-symbol
# notices, the REPL prompt, etc.) go to stderr.
#
# This test prints exactly one short string ("ok\n") via
# syscall 4.  When invoked as a Unix-process pipeline,
# stdout must contain exactly that string and nothing else.
#
# Driver (from a shell, not from this file):
#
#   $ spimulator -f tt.stderr_split.s > /tmp/out 2> /tmp/err
#   $ cat /tmp/out                                          # must be "ok\n"
#   ok
#   $ grep -c Loaded /tmp/err                               # must be 1 (banner on stderr)
#   1
#   $ grep -c Loaded /tmp/out                               # must be 0 (no banner on stdout)
#   0
#
# Historical regression: spim.c init set both console_out
# and message_out to stdout, so the "Loaded:" banner and
# "The following symbols are undefined" lines were mixed
# into the program's stdout, breaking pipelines.

        .data
ok:     .asciiz "ok\n"

        .text
        .globl main
main:
        la $a0, ok
        li $v0, 4
        syscall

        li $a0, 0
        li $v0, 17
        syscall
