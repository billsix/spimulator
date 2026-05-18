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


# C source — see 05-yes.c
#
#     __attribute__((noreturn)) void _start(void) {
#       for (;;)
#         print_string("y\n");
#     }
#
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
#VARIABLES:
#   $a0       syscall argument register (pinned at &yesString)
#   $v0       syscall selector (pinned at 4 = print_string)

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
