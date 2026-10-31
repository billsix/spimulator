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


# C source — see print1through10.c
#
#PURPOSE:  Print the integers 0 through 10, each on its own line.
#
#          STACK-VISIBLE STYLE: the loop counter `i` lives in a stack
#          slot, not a register.  A scratch register ($t0) holds a
#          working copy only while an instruction needs it, then the
#          value is stored straight back to i's slot.  This demo makes
#          NO `jal` (only syscalls, which preserve $ra), so it saves
#          $fp but not $ra.  Style reference:
#          tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  i           | -8($fp)     | $t0 (copy) | int       |   4   | loop counter 0..10; home on stack
#  (saved $fp) | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (no args; at beginningOfLoop with i = 3, about to test
#            and print)
#
#        main's stack frame            scratch
#   +-------------------------+       +---------------------------+
#   | -4($fp)  saved $fp      |       | $t0 = 3 (copy of i)
#   | -8($fp)  i       =  3   | <---- | $t0
#   +-------------------------+       +---------------------------+
#         $sp = $fp - 8
#
#   The loop loads i, tests i <= 10, prints i, then i++ and stores
#   back — printing 0 through 10, one per line, then exits 0.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args:
#  stdout:
#  | 0
#  | 1
#  | 2
#  | 3
#  | 4
#  | 5
#  | 6
#  | 7
#  | 8
#  | 9
#  | 10

        .data
nl:    .asciiz     "\n"

        .text
        .globl main
main:
        # ---- prologue: build an 8-byte frame (no $ra: no `jal`) ----
        addiu $sp, $sp, -8           # allocate the frame
        sw $fp, 4($sp)               # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 8            # $fp = top of our frame

        # int i = 0;
        li $t0, 0
        sw $t0, -8($fp)             # i = 0

beginningOfLoop:
        # while (i <= 10) { ...
        lw $t0, -8($fp)             # $t0 = i   (working copy)
        bgt $t0, 10, endOfLoop

        # print_int(i);
        lw $a0, -8($fp)             # arg = i
        li $v0, 1                   # syscall 1 = print_int
        syscall

        # print_string("\n");
        li $v0, 4                   # syscall 4 = print_string
        la $a0, nl
        syscall

        # i++;
        lw $t0, -8($fp)             # $t0 = i
        addiu $t0, $t0, 1           # i + 1
        sw $t0, -8($fp)             # store the new i back to its slot
        b beginningOfLoop            # } end of while

endOfLoop:
        # ---- epilogue: tear down the frame, return to the runtime --
        li $v0, 0                    # exit status 0
        lw $fp, -4($fp)             # restore caller's $fp
        addiu $sp, $sp, 8            # pop the frame
        jr $ra                       # $ra untouched (we made no `jal`)
