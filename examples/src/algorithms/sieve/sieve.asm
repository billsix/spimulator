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


# C source — see sieve.c


# Invocations:
#   spimulator -f sieve.asm           # primes up to 100 (default)
#   spimulator -f sieve.asm 200       # primes up to 200


#PURPOSE:  Sieve of Eratosthenes with N from argv.  Default 100.  Uses sbrk
#          (syscall 9) because the flag array size is only known at runtime; the
#          kernel zero-fills the new pages.
#
#          STACK-VISIBLE STYLE: the C locals `limit`, the sieve base pointer, and
#          the two loop counters `i`/`j` live in stack slots; a scratch register
#          holds only a working COPY (i*i and element addresses are transient).
#          The flag array itself lives in the sbrk heap (its base pointer is the
#          stack-homed local).  main makes `jal atoi`, so it saves $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  limit       | -12($fp)    | $t1 (copy) | int       |   4   | upper bound (argv[1] or 100)
#  sieve       | -16($fp)    | $t3 (copy) | char *    |   4   | flag array base (from sbrk; heap)
#  i           | -20($fp)    | $t0 (copy) | int       |   4   | outer counter (mark + print)
#  j           | -24($fp)    | $t0 (copy) | int       |   4   | inner mark counter (i*i, i*i+i, ...)
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | jal atoi
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#  No #CANONICAL block: the single output line ends with a trailing space (a
#  space after every prime), which a comment-based anchor can't carry.  Gated by
#  before/after output equivalence (default -> the 25 primes 2..97).

        .data
usageMsg:   .asciiz "usage: sieve [N]   (N >= 2)\n"

        .text
        .globl main
main:
        addiu $sp, $sp, -24
        sw $ra, 20($sp)              # -> -4($fp)
        sw $fp, 16($sp)              # -> -8($fp)
        addiu $fp, $sp, 24

        li $t0, 100
        sw $t0, -12($fp)             # limit = 100 (default)

        li $t0, 1
        beq $a0, $t0, alloc
        li $t0, 2
        bne $a0, $t0, usage
        lw $a0, 4($a1)
        jal atoi
        sw $v0, -12($fp)             # limit = atoi(argv[1])
        lw $t0, -12($fp)
        li $t1, 2
        blt $t0, $t1, usage

alloc:
        # sbrk(limit + 1) — returns base of new region in $v0
        lw $t0, -12($fp)
        addi $a0, $t0, 1
        li $v0, 9                    # 9 = sbrk
        syscall
        sw $v0, -16($fp)             # sieve base

        # ---- Mark composites ----
        li $t0, 2
        sw $t0, -20($fp)             # i = 2
mark_outer:
        lw $t0, -20($fp)             # i
        mult $t0, $t0
        mflo $t2                     # i*i (transient)
        lw $t1, -12($fp)             # limit
        bgt $t2, $t1, after_mark

        # if (!sieve[i])
        lw $t3, -16($fp)             # sieve base
        add $t1, $t3, $t0            # &sieve[i]
        lbu $t4, ($t1)
        bnez $t4, mark_next_i

        sw $t2, -24($fp)             # j = i*i

mark_inner:
        lw $t0, -24($fp)             # j
        lw $t1, -12($fp)             # limit
        bgt $t0, $t1, mark_next_i
        lw $t3, -16($fp)             # sieve base
        add $t1, $t3, $t0            # &sieve[j]
        li $t2, 1
        sb $t2, ($t1)                # sieve[j] = 1
        lw $t0, -24($fp)             # j
        lw $t4, -20($fp)             # i
        add $t0, $t0, $t4            # j += i
        sw $t0, -24($fp)
        j mark_inner

mark_next_i:
        lw $t0, -20($fp)             # i++
        addi $t0, $t0, 1
        sw $t0, -20($fp)
        j mark_outer

after_mark:
        # ---- Print survivors ----
        li $t0, 2
        sw $t0, -20($fp)             # i = 2
print_outer:
        lw $t0, -20($fp)             # i
        lw $t1, -12($fp)             # limit
        bgt $t0, $t1, print_done

        lw $t3, -16($fp)             # sieve base
        add $t1, $t3, $t0            # &sieve[i]
        lbu $t4, ($t1)
        bnez $t4, print_next_i

        lw $a0, -20($fp)             # i
        li $v0, 1
        syscall
        li $a0, ' '
        li $v0, 11
        syscall

print_next_i:
        lw $t0, -20($fp)             # i++
        addi $t0, $t0, 1
        sw $t0, -20($fp)
        j print_outer

print_done:
        li $a0, '\n'
        li $v0, 11
        syscall

        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 24
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
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
