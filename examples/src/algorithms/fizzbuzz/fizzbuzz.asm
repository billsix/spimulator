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


# C source — see fizzbuzz.c
# Invocations:
#   spimulator -f fizzbuzz.asm             # 1..100 (default)
#   spimulator -f fizzbuzz.asm 30          # 1..30


#PURPOSE:  FizzBuzz with upper bound from argv.  Default N=100.  Modulo via
#          `div`+`mfhi`, a multi-way branch cascade (15 check first), mixed
#          string/int output.
#
#          STACK-VISIBLE STYLE: the C locals `n` (upper bound) and `i` (loop
#          counter) live in stack slots; a scratch register holds only a working
#          COPY (and the modulo remainder is a transient in $t1).  argc/argv are
#          used once at entry, so they stay in $a0/$a1.  main makes `jal atoi`,
#          so it saves $ra.  atoi is a shared leaf helper.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  argc        | —           | $a0        | int       |   4   | entry only; 1 -> default, 2 -> parse N
#  argv        | —           | $a1        | char **   |   4   | entry only; argv[1] for atoi
#  n           | -12($fp)    | $t1 (copy) | int       |   4   | upper bound (argv[1] or 100)
#  i           | -16($fp)    | $t0 (copy) | int       |   4   | loop counter 1..n
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | jal atoi
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args: 15
#  stdout:
#  | 1
#  | 2
#  | Fizz
#  | 4
#  | Buzz
#  | Fizz
#  | 7
#  | 8
#  | Fizz
#  | Buzz
#  | 11
#  | Fizz
#  | 13
#  | 14
#  | FizzBuzz

        .data
fizzMsg:        .asciiz "Fizz"
buzzMsg:        .asciiz "Buzz"
fizzBuzzMsg:    .asciiz "FizzBuzz"
nlMsg:          .asciiz "\n"
usageMsg:       .asciiz "usage: fizzbuzz [N]\n"

        .text
        .globl main
main:
        addiu $sp, $sp, -16
        sw $ra, 12($sp)              # -> -4($fp)
        sw $fp, 8($sp)               # -> -8($fp)
        addiu $fp, $sp, 16

        li $t0, 100
        sw $t0, -12($fp)             # n = 100 (default)

        # argc dispatch   (argc arrives in $a0)
        li $t0, 1
        beq $a0, $t0, run            # argc == 1 -> default
        li $t0, 2
        bne $a0, $t0, usage          # argc != 2 -> usage
        lw $a0, 4($a1)               # parse argv[1] as N
        jal atoi
        sw $v0, -12($fp)             # n = atoi(argv[1])

run:
        li $t0, 1
        sw $t0, -16($fp)             # i = 1

loop:
        lw $t0, -16($fp)             # i
        lw $t1, -12($fp)             # n
        bgt $t0, $t1, done

        # if (i % 15 == 0)
        lw $t0, -16($fp)
        li $t1, 15
        div $t0, $t1
        mfhi $t1
        bnez $t1, try3
        la $a0, fizzBuzzMsg
        li $v0, 4
        syscall
        j newline
try3:
        lw $t0, -16($fp)
        li $t1, 3
        div $t0, $t1
        mfhi $t1
        bnez $t1, try5
        la $a0, fizzMsg
        li $v0, 4
        syscall
        j newline
try5:
        lw $t0, -16($fp)
        li $t1, 5
        div $t0, $t1
        mfhi $t1
        bnez $t1, printNum
        la $a0, buzzMsg
        li $v0, 4
        syscall
        j newline
printNum:
        lw $a0, -16($fp)             # i
        li $v0, 1
        syscall

newline:
        la $a0, nlMsg
        li $v0, 4
        syscall

        lw $t0, -16($fp)             # i++
        addi $t0, $t0, 1
        sw $t0, -16($fp)
        j loop

done:
        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 16
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall


# ---------- atoi subroutine (shared leaf helper, register-based) ----
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
