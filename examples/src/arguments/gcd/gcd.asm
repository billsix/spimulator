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


# C source — see gcd.c
# Invocation:
#   spimulator -f gcd.asm 1071 462


#PURPOSE:  Compute gcd(A, B) by the Euclidean algorithm with A and
#          B from argv[1] and argv[2].  First demo with TWO numeric
#          args, so atoi runs twice in sequence.
#
#NOTES:    Spim's `div` pseudo-instruction stores quotient in `lo`
#          and remainder in `hi`.  Euclidean GCD only needs the
#          remainder, so we read `hi` with `mfhi` and ignore `lo`.
#
#          STACK-VISIBLE STYLE: a and b live in stack slots and
#          survive the whole loop there; a scratch register holds a
#          copy only while an instruction needs it.  argv and $ra
#          also live on the stack because this routine makes `jal`s
#          (atoi clobbers $ra and may touch $a*).  The one value kept
#          in a register is `t = a % b`: a transient sub-expression
#          consumed immediately (stored straight into b's slot), not
#          a C local that persists across statements.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register    | C type    | bytes | role / notes
#  ------------+-------------+-------------+-----------+-------+--------------------------
#  argc        | —           | $a0         | int       |   4   | entry only; tested, then dead
#  argv        | -12($fp)    | $a1 (copy)  | char **   |   4   | saved to fetch argv[2] post-jal
#  a           | -16($fp)    | $t0 (copy)  | int       |   4   | gcd's first operand
#  b           | -20($fp)    | $t1 (copy)  | int       |   4   | gcd's second operand
#  t (a % b)   | —           | $t2         | int       |   4   | remainder; transient temporary
#  (saved $ra) | -4($fp)     | —           | code addr |   4   | return into the runtime
#  (saved $fp) | -8($fp)     | —           | frame ptr |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (input: gcd 1071 462, at gcd_loop iteration 2,
#            a and b loaded; about to compute a % b)
#
#        main's stack frame                scratch
#   +----------------------------+        +------------------------+
#   | -4($fp)  saved $ra         |        | $t0 = 462 (copy of a)
#   | -8($fp)  saved $fp         |        | $t1 = 147 (copy of b)
#   | -12($fp) argv  = <ptr>     |        +------------------------+
#   | -16($fp) a     = 462       | <----- | $t0
#   | -20($fp) b     = 147       | <----- | $t1
#   +----------------------------+
#         $sp = $fp - 20
#
#   `div $t0,$t1; mfhi $t2` sets t = 462 % 147 = 21; then a <- b (147)
#   and b <- t (21) are stored.  Iterations run (1071,462) -> (462,147)
#   -> (147,21) -> (21,0); at b = 0 the result a = 21 is printed.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args: 1071 462
#  stdout:
#  | 21

        .data
usageMsg:   .asciiz "usage: gcd A B\n"

        .text
        .globl main
main:
        # ---- prologue: build main's 20-byte stack frame ------------
        addiu $sp, $sp, -20          # allocate the frame
        sw $ra, 16($sp)              # save $ra          -> -4($fp)
        sw $fp, 12($sp)              # save caller's $fp -> -8($fp)
        addiu $fp, $sp, 20           # $fp = top of our frame

        # if (argc != 3) usage;   (argc arrives in $a0)
        li $t0, 3
        bne $a0, $t0, usage

        # argv (in $a1) is needed after the first jal to reach
        # argv[2], so give it a stack home before atoi runs.
        sw $a1, -12($fp)             # argv

        # a = atoi(argv[1]);
        lw $a0, 4($a1)               # argv[1]
        jal atoi                     # $v0 = A  ($ra safe in its slot)
        sw $v0, -16($fp)             # a = $v0

        # b = atoi(argv[2]);
        lw $t0, -12($fp)             # reload argv
        lw $a0, 8($t0)               # argv[2]
        jal atoi
        sw $v0, -20($fp)             # b = $v0

gcd_loop:
        # while (b != 0) { t = a % b; a = b; b = t; }
        lw $t1, -20($fp)             # $t1 = b
        beq $t1, $0, gcd_done
        lw $t0, -16($fp)             # $t0 = a
        div $t0, $t1                 # hi = a % b, lo = a / b
        mfhi $t2                     # t = a % b
        sw $t1, -16($fp)             # a = b
        sw $t2, -20($fp)             # b = t
        j gcd_loop

gcd_done:
        # print_int(a);   -- a is the result
        lw $a0, -16($fp)             # a
        li $v0, 1                    # 1 = print_int (signed decimal)
        syscall

        # print_char('\n');
        li $v0, 11
        li $a0, '\n'
        syscall

        # ---- epilogue: tear down the frame, return to the runtime --
        li $v0, 0                    # exit status 0
        lw $ra, -4($fp)              # restore $ra
        lw $fp, -8($fp)              # restore caller's $fp
        addiu $sp, $sp, 20           # pop the frame
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall


# ---------- atoi subroutine -----------------------------------------
# Shared leaf helper (string -> signed int), kept register-based: arg
# in $a0, return in $v0, calls nothing, so it needs no frame.
#
#  SYMBOL TABLE (routine: atoi — leaf helper, register-based)
#
#  C variable | memory addr | register | C type | bytes | role / notes
#  -----------+-------------+----------+--------+-------+------------------------------
#  (arg) str  | —           | $a0      | char * |   4   | address of the string, walked
#  value      | —           | $v0      | int    |   4   | accumulator; becomes the return
#  sign       | —           | $t1      | int    |   4   | +1 or -1
#  digit      | —           | $t0      | int    |   4   | one byte decoded to 0..9
#  ten        | —           | $t2      | int    |   4   | constant multiplier
#
# Input:  $a0 = address of null-terminated decimal string
# Output: $v0 = parsed signed int
# Trashes: $t0..$t2
atoi:
        li $v0, 0                    # value = 0
        li $t1, 1                    # sign = +1

        lb $t0, ($a0)
        bne $t0, '-', atoi_loop
        li $t1, -1
        addi $a0, $a0, 1

atoi_loop:
        lb $t0, ($a0)
        blt $t0, '0', atoi_done
        bgt $t0, '9', atoi_done
        addi $t0, $t0, -48           # digit value
        li $t2, 10
        mult $v0, $t2
        mflo $v0
        add $v0, $v0, $t0
        addi $a0, $a0, 1
        j atoi_loop

atoi_done:
        mult $v0, $t1                # apply sign
        mflo $v0
        jr $ra
