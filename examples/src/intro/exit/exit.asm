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


# C source — see exit.c
#
#     __attribute__((noreturn)) void _start(void) {
#       os_exit(0);
#     }
#
# This is the simplest possible program.  It performs exactly
# one operation: it asks the kernel to terminate the process
# and hands the kernel a status code (0 here) to make available
# to the parent shell as `$?`.
#
#
#PURPOSE:  Demonstrate the syscall mechanism on the smallest
#          possible program — set up arguments in registers,
#          place the syscall number in $v0, execute `syscall`.
#
#
# How a syscall works in spim
# ===========================
# Spim emulates a tiny operating system that responds to the
# `syscall` instruction.  When the CPU executes `syscall`, the
# simulator looks at register $v0 to decide WHICH service the
# program is asking for, and at registers $a0..$a3 for any
# arguments.  Some syscalls also return a value in $v0.
#
# Syscall 17 — "exit with status" — takes the status code in
# $a0 and never returns.  The convention is:
#
#     $v0 = 17           # the syscall number
#     $a0 = status       # the value the shell sees as `echo $?`
#     syscall            # transfer control to the kernel
#
# This is the spim equivalent of what Programming from the
# Ground Up calls "the exit system call" — on i386 Linux that's
# `int $0x80` with `%eax = 1` and `%ebx = status`.  Same idea:
# place the syscall number and arguments in agreed-upon
# registers, then execute a trap instruction.
#
#
# Why `syscall 17` and not `syscall 10`?
# ======================================
# Spim defines TWO exit syscalls:
#
#     syscall 10  — exit with status 0, always
#     syscall 17  — exit with status in $a0  (called "exit2")
#
# Use 17.  The status code IS the lesson — without it, you
# can't observe anything via `$?`.
#
#
# An alternative: `jr $ra`
# ========================
# A program's `main` is also allowed to simply return.  Spim's
# startup code (`__start` in exceptions.s) takes whatever value
# is in $v0 at the return and passes it to syscall 17 for you.
# So this five-line program is equivalent to:
#
#     main:
#         li $v0, 0          # the status code to return
#         jr $ra             # return to the runtime
#
# The catch: that DEPENDS on $v0 holding the desired status at
# the moment of `jr $ra`.  Every `syscall` you make along the
# way clobbers $v0 with the syscall number, so a program that
# does any I/O before returning has to remember to set $v0 = N
# explicitly before its final `jr $ra`.  Every other demo in
# this tree does exactly that — see the `li $v0, 0` line right
# before the closing `jr $ra` in helloworld, 02, 03, etc.
#
# The explicit `li $v0, 17; syscall` form below sidesteps the
# discipline entirely.  Both forms are valid; the explicit form
# is more honest about what's happening.
#
#
#SYMBOL TABLE
#----------------------------------------------------------------
# C source     | MIPS location | Notes
#----------------------------------------------------------------
# 0 (status)   | $a0           | first syscall argument
# (syscall #)  | $v0           | which service is being requested
#----------------------------------------------------------------


        .text
        .globl main
main:
# doc-region-begin exit syscall
        # os_exit(0);
        li $a0, 0                    # the status code to return
        li $v0, 17                   # syscall 17 = exit2 (status in $a0)
        syscall                      # transfer control to the kernel
# doc-region-end exit syscall

        # Execution never reaches here.  syscall 17 does not return.
