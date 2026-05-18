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


# C source — see 06-fizzbuzz.c
#
#     int my_main(int argc, char **argv) {
#       int n = (argc == 2) ? parse_int(argv[1]) : 100;
#       for (int i = 1; i <= n; i++) {
#         if (i % 15 == 0) print_string("FizzBuzz");
#         ...
#       }
#       return 0;
#     }
#
# Invocations:
#   spimulator -f 06-fizzbuzz.asm             # 1..100 (default)
#   spimulator -f 06-fizzbuzz.asm 30          # 1..30


#PURPOSE:  FizzBuzz with upper bound from argv.  Default N=100.
#          Three things going on at once:
#            - modulo via `div` + `mfhi`
#            - multi-way branching cascade
#            - mixed-format output (string OR int per line)
#
#NOTES:    The 15-check comes FIRST so the FizzBuzz lines hit
#          BEFORE the 3-only check would match them.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     argc          $a0 at entry only
#     argv          $a1 at entry only
#     n             $s1                  (upper bound from argv, or 100)
#     i             $t0                  (loop counter, 1..n)
#     i % {15,3,5}  $t1                  (divisor going in, remainder
#                                          after mfhi)
#
#   In atoi (same as 20-factorial / 21-gcd):
#     standard.
#
#   Cross-call saves: $s0 ← runtime's $ra; $s1 ← n (held across
#   `jal atoi` and through the body).

        .data
fizzMsg:        .asciiz "Fizz"
buzzMsg:        .asciiz "Buzz"
fizzBuzzMsg:    .asciiz "FizzBuzz"
nlMsg:          .asciiz "\n"
usageMsg:       .asciiz "usage: fizzbuzz [N]\n"

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 100                  # n = 100 (default)

        # argc dispatch
        li $t0, 1
        beq $a0, $t0, run            # argc == 1 -> default
        li $t0, 2
        bne $a0, $t0, usage          # argc != 2 -> usage

        # parse argv[1] as N
        lw $a0, 4($a1)
        jal atoi
        move $s1, $v0

run:
        li $t0, 1                    # i = 1

loop:
        bgt $t0, $s1, done

        # if (i % 15 == 0)
        li $t1, 15
        div $t0, $t1
        mfhi $t1
        bnez $t1, try3
        la $a0, fizzBuzzMsg
        li $v0, 4
        syscall
        j newline

try3:
        li $t1, 3
        div $t0, $t1
        mfhi $t1
        bnez $t1, try5
        la $a0, fizzMsg
        li $v0, 4
        syscall
        j newline

try5:
        li $t1, 5
        div $t0, $t1
        mfhi $t1
        bnez $t1, printNum
        la $a0, buzzMsg
        li $v0, 4
        syscall
        j newline

printNum:
        move $a0, $t0
        li $v0, 1
        syscall

newline:
        la $a0, nlMsg
        li $v0, 4
        syscall

        addi $t0, $t0, 1
        j loop

done:
        move $ra, $s0
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall


# ---------- atoi subroutine -----------------------------------------
# Standard atoi pattern, same as 20-factorial.
atoi:
        li $v0, 0
        li $t1, 1
        lb $t0, ($a0)
        bne $t0, '-', atoi_loop
        li $t1, -1
        addi $a0, $a0, 1
atoi_loop:
        lb $t0, ($a0)
        blt $t0, '0', atoi_done
        bgt $t0, '9', atoi_done
        addi $t0, $t0, -48
        li $t2, 10
        mult $v0, $t2
        mflo $v0
        add $v0, $v0, $t0
        addi $a0, $a0, 1
        j atoi_loop
atoi_done:
        mult $v0, $t1
        mflo $v0
        jr $ra
