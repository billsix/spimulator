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
#
#     int gcd(int a, int b) {
#       while (b != 0) { int t = a % b; a = b; b = t; }
#       return a;
#     }
#
#     int my_main(int argc, char **argv) {
#       if (argc != 3) { print_string("usage: gcd A B\n"); return 1; }
#       int a = parse_int(argv[1]);
#       int b = parse_int(argv[2]);
#       print_int(gcd(a, b));
#       print_char('\n');
#       return 0;
#     }
#
# Invocation:
#   spimulator -f gcd.asm 1071 462


#PURPOSE:  Compute gcd(A, B) by the Euclidean algorithm with A and
#          B from argv[1] and argv[2].  First demo with TWO numeric
#          args, so atoi runs twice in sequence and we hold A in a
#          callee-save register while parsing B.
#
#NOTES:    Spim's `div` pseudo-instruction stores quotient in `lo`
#          and remainder in `hi`.  Euclidean GCD only needs the
#          remainder, so we ignore `lo` after the divide.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     argc          $a0 at entry only      (clobbered after first syscall)
#     argv          $a1 at entry,          (held in $s2 across `jal atoi`
#                   then $s2 briefly        so we can still compute argv[2]
#                                           after parsing argv[1])
#     A             $s1                    (gcd's first operand;
#                                            atoi(argv[1]))
#     B             $s2                    (gcd's second operand;
#                                            atoi(argv[2]) — overwrites
#                                            the parked argv after the
#                                            second jal)
#     A % B         $t0                    (Euclidean step's remainder,
#                                            transient between mfhi and
#                                            the move into $s1/$s2)
#
#   In atoi subroutine:
#     value         $v0                    (accumulator — becomes the return)
#     sign          $t1                    (+1 or -1)
#     digit         $t0                    (one byte, decoded to 0..9)
#     ten           $t2                    (constant multiplier)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra      (across BOTH `jal atoi`s; restored
#                                  before the final `jr $ra`)
#     $s2   <- argv               (across the FIRST `jal atoi`, so the
#                                  second can compute argv[2]; then
#                                  overwritten with B for the gcd loop)
#     $s1   <- A                  (across the SECOND `jal atoi`, so A
#                                  is still available when we enter the
#                                  gcd loop with both A and B in hand)
#
#     This is the demo where the cross-call save discipline first
#     becomes load-bearing: drop any of these three saves and the
#     program produces wrong answers.
#
#   Volatile (no preserved meaning across a syscall):
#     $a0   syscall arg / atoi arg / print_int arg
#     $v0   syscall selector / function return value

        .data
usageMsg:   .asciiz "usage: gcd A B\n"

        .text
        .globl main
main:
        move $s0, $ra

        # if (argc != 3) usage
        li $t0, 3
        bne $a0, $t0, usage

        # A = atoi(argv[1]);  argv is in $a1
        lw $a0, 4($a1)               # argv[1]
        move $s2, $a1                # park argv (atoi trashes $a0; need $a1 too)
        jal atoi
        move $s1, $v0                # save A

        # B = atoi(argv[2])
        lw $a0, 8($s2)               # argv[2]
        jal atoi
        move $s2, $v0                # save B

gcd_loop:
        # while (B != 0) { t = A % B; A = B; B = t; }
        beq $s2, $0, gcd_done
        div $s1, $s2                 # hi = A % B, lo = A / B
        mfhi $t0                     # t = A % B
        move $s1, $s2                # A = B
        move $s2, $t0                # B = t
        j gcd_loop

gcd_done:
        # print_int(A)  -- A is the result
        move $a0, $s1
        li $v0, 1                    # 1 = print_int (signed decimal)
        syscall

        # print_char('\n')
        li $v0, 11
        li $a0, '\n'
        syscall

        move $ra, $s0
        li $v0, 0                    # exit status: __start passes this through syscall 17
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall


# ---------- atoi subroutine -----------------------------------------
# Same shape as factorial's atoi.  Repeated here per the
# per-demo "self-contained" convention.
# Input:  $a0 = address of null-terminated decimal string
# Output: $v0 = parsed signed int
# Trashes: $t0..$t3
atoi:
        li $v0, 0
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
