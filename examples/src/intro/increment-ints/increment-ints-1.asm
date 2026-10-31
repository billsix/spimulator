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


# C source — see increment-ints.c


#PURPOSE:  Demonstrate pre- and post-increment of integer variables.
#          Two locals `a` and `b` both start at 5.  Prints
#          ++a + 5, then a, then b++ + 5, then b — each on its own
#          line (11, 6, 10, 6).
#
#          STACK-VISIBLE STYLE: `a` and `b` live in stack slots, not
#          registers.  A scratch register ($t0) holds a working copy
#          only while an instruction needs it, then the value is
#          stored straight back.  This demo makes NO `jal`, so it
#          saves no $ra (syscalls preserve $ra) — only the frame
#          pointer is saved.  (Save $ra on the stack only when a
#          routine calls another with `jal`; see factorial.)
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+-----------------------
#  a           | -8($fp)     | $t0 (copy) | int       |   4   | first int; home on stack
#  b           | -12($fp)    | $t0 (copy) | int       |   4   | second int; home on stack
#  (saved $fp) | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (no args; just after `++a` is computed and stored,
#            before the first print)
#
#        main's stack frame             scratch
#   +--------------------------+       +---------------------------+
#   | -4($fp)  saved $fp       |       | $t0 = 6 (copy of a, ++'d)
#   | -8($fp)  a       =  6    | <---- | $t0
#   | -12($fp) b       =  5    |       +---------------------------+
#   +--------------------------+
#         $sp = $fp - 12
#
#   Then prints 11 (a+5 with new a), 6 (a), 10 (OLD b + 5), 6 (b
#   after b++).
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args:
#  stdout:
#  | 11
#  | 6
#  | 10
#  | 6

        .data
nl:    .asciiz     "\n"

        .text
        .globl main
main:
        # ---- prologue: build a 12-byte frame (no $ra: no `jal`) ----
        addiu $sp, $sp, -12          # allocate the frame
        sw $fp, 8($sp)               # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 12           # $fp = top of our frame

        # a = b = 5;
        li $t0, 5
        sw $t0, -8($fp)              # a = 5
        sw $t0, -12($fp)             # b = 5

        # print_int(++a + 5);   -- increment a FIRST, then use it (prints 11)
        lw $t0, -8($fp)             # $t0 = a
        addiu $t0, $t0, 1           # ++a
        sw $t0, -8($fp)             # store the new a back to its slot
        addiu $a0, $t0, 5           # a + 5   (with the new a)
        li $v0, 1                   # syscall 1 = print_int
        syscall

        # print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall

        # print_int(a);              -- prints 6
        lw $a0, -8($fp)             # a
        li $v0, 1
        syscall

        # print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall

        # print_int(b++ + 5);        -- use the OLD b (prints 10), then b++
        lw $t0, -12($fp)            # $t0 = b   (old value)
        addiu $a0, $t0, 5           # b + 5   (with the old b)
        li $v0, 1
        syscall
        addiu $t0, $t0, 1           # b++
        sw $t0, -12($fp)            # store the new b back

        # print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall

        # print_int(b);              -- prints 6
        lw $a0, -12($fp)            # b
        li $v0, 1
        syscall

        # print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall

        # ---- epilogue: tear down the frame, return to the runtime --
        li $v0, 0                    # exit status 0
        lw $fp, -4($fp)             # restore caller's $fp
        addiu $sp, $sp, 12           # pop the frame
        jr $ra                       # $ra untouched (we made no `jal`)
