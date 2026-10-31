# Copyright (c) 2021-2026 William Emerison Six
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


# C source — see yes.c
# Suckless `sbase/yes` in three real instructions plus a load of
# constants done once.  The print_string syscall reads $v0 and $a0
# but doesn't write to them, so we can set both ONCE outside the
# loop and just `syscall; j loop` inside.


#PURPOSE:  Print "y\n" forever.  The tightest possible Unix-utility
#          loop — the inner body is just `syscall` and `j`.
#
#NOTES:    Runs forever.  On a real Unix system, `yes | head -3`
#          terminates via SIGPIPE when the reader closes its end of
#          the pipe; spimulator has no SIGPIPE so the program only
#          stops on Ctrl+C inside the simulator.
#
#          STACK-VISIBLE STYLE: this program has NO C local variables
#          — just a fixed string in .data — and makes NO `jal`, so
#          there is no stack frame.  The string lives in .data; its
#          ADDRESS is pinned in $a0 for the whole run.  There is no
#          #CANONICAL block: the loop never terminates, so the
#          value-checker (which runs the demo to completion) cannot
#          anchor it.  Style reference:
#          tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable | memory addr       | register   | C type  | bytes | role / notes
#  -----------+-------------------+------------+---------+-------+---------------------------
#  "y\n"      | yesString (.data) | $a0 (addr) | char[3] |   3   | string literal (2 bytes + NUL); address pinned in $a0, printed forever

        .data
yesString:
        .asciiz     "y\n"

        .text
        .globl main
main:
        li $v0, 4                    # syscall 4 = print_string (set ONCE)
        la $a0, yesString            # arg = address of "y\n"  (set ONCE)

forever:
        syscall                      # print "y\n"
        j forever                    # ... and again
