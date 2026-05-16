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


# C source — see 25-fizzbuzz-1.c
#
#     for (int i = 1; i <= 100; i++) {
#       if (i % 15 == 0)      print_string("FizzBuzz");
#       else if (i % 3 == 0)  print_string("Fizz");
#       else if (i % 5 == 0)  print_string("Buzz");
#       else                  print_int(i);
#       print_char('\n');
#     }


#PURPOSE:  FizzBuzz, 1..100.  First demo from PLAN-cs-demos.md.
#          Introduces three things at once:
#            - modulo via `div` + `mfhi`  (the high/lo product
#              registers are also where divu/div leave the
#              remainder/quotient)
#            - multi-way branching via a fall-through cascade of
#              conditional branches (the C `if/else if` chain)
#            - mixed-format output: either print_string for a
#              fizz/buzz word, or print_int for the number
#
#NOTES:    The 15-check comes FIRST.  Putting it last (checking
#          for "both 3 and 5 divide i" after the 3- and 5-checks)
#          would print "Fizz" or "Buzz" for the FizzBuzz lines
#          and miss the combined case entirely.  Order matters.
#
#          `div $t0, $t1` puts the quotient in `lo` (mflo) and
#          the remainder in `hi` (mfhi).  We only want the
#          remainder, so each test reads only `mfhi`.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     i             $t0                  (1..100 loop counter)
#     i % {15,3,5}  $t1                  (divisor going in, then
#                                          remainder after `mfhi`;
#                                          reused per-test before each
#                                          fresh div)
#     fizzMsg, buzzMsg, fizzBuzzMsg, nlMsg
#                   `.data` strings      (printed via syscall 4)
#
#   No subroutine calls; no Cross-call saves section.  Two
#   $t-regs reused across every syscall per the spim-preserves-$t
#   convention (same shape as 06/12/13/17).
#
#   Volatile (no preserved meaning across a syscall, by convention):
#     $a0   syscall arg
#     $v0   syscall selector (1 = print_int, 4 = print_string)

        .data
fizzMsg:        .asciiz "Fizz"
buzzMsg:        .asciiz "Buzz"
fizzBuzzMsg:    .asciiz "FizzBuzz"
nlMsg:          .asciiz "\n"

        .text
        .globl main
main:
        li $t0, 1                    # i = 1

loop:
        # while (i <= 100)
        li $t1, 100
        bgt $t0, $t1, done

        # if (i % 15 == 0) -> FizzBuzz
        li $t1, 15
        div $t0, $t1                 # lo = i / 15, hi = i % 15
        mfhi $t1                     # $t1 = i % 15
        bnez $t1, try3
        la $a0, fizzBuzzMsg
        li $v0, 4                    # 4 = print_string
        syscall
        j newline

try3:
        # else if (i % 3 == 0) -> Fizz
        li $t1, 3
        div $t0, $t1
        mfhi $t1
        bnez $t1, try5
        la $a0, fizzMsg
        li $v0, 4
        syscall
        j newline

try5:
        # else if (i % 5 == 0) -> Buzz
        li $t1, 5
        div $t0, $t1
        mfhi $t1
        bnez $t1, printNum
        la $a0, buzzMsg
        li $v0, 4
        syscall
        j newline

printNum:
        # else -> print_int(i)
        move $a0, $t0
        li $v0, 1                    # 1 = print_int
        syscall

newline:
        # print_char('\n')   -- via print_string of nlMsg, since
        # 11 = print_char would also work but we already have the
        # string handy and the cost is identical.
        la $a0, nlMsg
        li $v0, 4
        syscall

        addi $t0, $t0, 1             # i++
        j loop

done:
        li $v0, 0
        jr $ra
