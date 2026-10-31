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


# C source — see print-out-ascii.c


#PURPOSE:  Walk a signed value from -128 up to 127, printing each on
#          its own line.  Demonstrates a do-while loop and the
#          two's-complement representation of a negative number
#          (-128 is stored as 0xffffff80 in a 32-bit register).
#
#          STACK-VISIBLE STYLE: the C local `c` lives in a stack slot;
#          a scratch register ($t0) holds a working COPY only while an
#          instruction needs it, then the value is stored back.  This
#          demo makes NO `jal` (only syscalls, which preserve $ra), so
#          it saves $fp but not $ra.  (The old version held c in $t0
#          for the whole run.)
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#          No #CANONICAL block: the output is all 256 values -128..127,
#          one per line — too long for a comment anchor.  Correctness
#          is gated by before/after output equivalence (256 lines;
#          first -128, last 127).
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  c           | -8($fp)     | $t0 (copy) | char      |   4   | signed counter -128..127 (held sign-extended in a word slot)
#  (saved $fp) | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (no args; mid-loop, just after c was incremented to 0)
#
#        main's stack frame        scratch
#   +----------------------+      +--------------------+
#   | -4($fp) saved $fp    |      | $t0 = 0 (copy of c)
#   | -8($fp) c      =  0  | <--- | $t0
#   +----------------------+      +--------------------+
#         $sp = $fp - 8
#
#   Prints -128..127 (256 lines) — the negatives show the two's
#   complement wrap of the signed value — then exits 0.

        .data
nl:    .asciiz     "\n"

        .text
        .globl main
main:
        # ---- prologue: build an 8-byte frame (no $ra: no `jal`) ----
        addiu $sp, $sp, -8
        sw $fp, 4($sp)               # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 8

        # char c = CHAR_MIN;
        li $t0, -128                 # -128 is stored as 0xffffff80
        sw $t0, -8($fp)              # c = -128

        # Pre-loop print — emit CHAR_MIN before entering the do-while body.
        lw $a0, -8($fp)              # c
        li $v0, 1                    # syscall 1 = print_int
        syscall
        li $v0, 4                    # syscall 4 = print_string
        la $a0, nl
        syscall

loopBegin:
        # c = c + 1;                  -- the do-while body
        lw $t0, -8($fp)              # $t0 = c
        addi $t0, $t0, 1
        sw $t0, -8($fp)              # store the new c back

        # print_int(c);
        lw $a0, -8($fp)
        li $v0, 1
        syscall
        # print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall

        # } while (c != CHAR_MAX);    -- branch back while c != 127
        lw $t0, -8($fp)              # $t0 = c
        bne $t0, 127, loopBegin

        # ---- epilogue ----------------------------------------------
        li $v0, 0                    # exit status 0
        lw $fp, -4($fp)              # restore caller's $fp
        addiu $sp, $sp, 8            # pop the frame
        jr $ra                       # $ra untouched (we made no `jal`)
