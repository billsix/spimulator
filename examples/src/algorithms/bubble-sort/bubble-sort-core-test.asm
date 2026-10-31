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


# C source — see bubble-sort-core-test.c

# Invocation (no input; runs the test battery):
#   spimulator -f bubble-sort-core-test.asm   ->  "<input> => <sorted>" per case

# PURPOSE:  Unit tests for the bubble-sort core.  bubble_sort($a0=a,$a1=n) sorts
#           an array in place (no I/O), so it can be driven by a table of
#           hardcoded arrays.  main loops the cases, printing "<input> => <sorted>".
#
#           STACK-VISIBLE STYLE: main's C locals — the case index i, the current
#           array pointer, and its length n — live in stack slots.  bubble_sort
#           (the algorithm under test) is a leaf: it homes its own `i`/`j` on its
#           frame (args a=$a0, n=$a1).  The case arrays are file-scope data in
#           .data.  print_ints is a register-based display helper.  main makes
#           jals, so it saves $ra.
#           Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE: main — i -> -12($fp), arr -> -16($fp), n -> -20($fp); saved
#  $ra/$fp at -4/-8($fp).  bubble_sort — i -> -8($fp), j -> -12($fp); saved $fp
#  at -4($fp); a/n in $a0/$a1.
#
#  No #CANONICAL block: the empty-array case prints the line " => " (a trailing
#  space), which a comment-based anchor can't carry.  Golden-tested via run-demo.sh.

        .data
arrow:      .asciiz " => "
c0:         .word 5, 3, 8, 1, 9, 2, 7
c1:         .word 1, 2, 3, 4, 5
c2:         .word 5, 4, 3, 2, 1
c3:         .word 3, 1, 3, 2, 1
c4:         .word 42
c5:         .word -3, 5, -1, 0, 2
cases:      .word c0, c1, c2, c3, c4, 0, c5    # 0 = the empty case (n = 0)
lens:       .word 7, 5, 5, 5, 1, 0, 5

        .text
        .globl main
main:
        addiu $sp, $sp, -24
        sw   $ra, 20($sp)            # -> -4($fp)
        sw   $fp, 16($sp)            # -> -8($fp)
        addiu $fp, $sp, 24

        li   $t0, 0
        sw   $t0, -12($fp)           # i = 0
mt_loop:
        lw   $t0, -12($fp)           # i
        li   $t1, 7                  # case_count = 7
        bge  $t0, $t1, mt_done
        sll  $t2, $t0, 2
        la   $t1, cases
        add  $t1, $t1, $t2
        lw   $t3, 0($t1)             # arr ptr
        sw   $t3, -16($fp)           # arr
        la   $t1, lens
        add  $t1, $t1, $t2
        lw   $t3, 0($t1)             # n
        sw   $t3, -20($fp)           # n

        lw   $a0, -16($fp)           # print the input array
        lw   $a1, -20($fp)
        jal  print_ints
        la   $a0, arrow              # " => "
        li   $v0, 4
        syscall
        lw   $a0, -16($fp)           # sort in place
        lw   $a1, -20($fp)
        jal  bubble_sort
        lw   $a0, -16($fp)           # print the sorted array
        lw   $a1, -20($fp)
        jal  print_ints
        li   $v0, 11                 # newline
        li   $a0, 10
        syscall

        lw   $t0, -12($fp)           # i++
        addi $t0, $t0, 1
        sw   $t0, -12($fp)
        j    mt_loop
mt_done:
        li   $v0, 0
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 24
        jr   $ra


# ---------- bubble_sort($a0 = a, $a1 = n) — sort in place (leaf) ----------
bubble_sort:
        addiu $sp, $sp, -16
        sw   $fp, 12($sp)            # -> -4($fp)
        addiu $fp, $sp, 16
        li   $t0, 0
        sw   $t0, -8($fp)            # i = 0
bs_outer:
        lw   $t0, -8($fp)            # i
        addi $t1, $a1, -1            # n - 1
        bge  $t0, $t1, bs_done       # for (i = 0; i < n-1; i++)
        li   $t0, 0
        sw   $t0, -12($fp)           # j = 0
bs_inner:
        lw   $t2, -12($fp)           # j
        lw   $t0, -8($fp)            # i
        sub  $t3, $a1, $t0
        addi $t3, $t3, -1            # n - 1 - i
        bge  $t2, $t3, bs_inner_done # for (j = 0; j < n-1-i; j++)
        sll  $t4, $t2, 2
        add  $t5, $a0, $t4           # &a[j]
        lw   $t6, 0($t5)             # a[j]
        lw   $t7, 4($t5)             # a[j+1]
        ble  $t6, $t7, bs_noswap     # if (a[j] > a[j+1]) swap
        sw   $t7, 0($t5)
        sw   $t6, 4($t5)
bs_noswap:
        lw   $t2, -12($fp)
        addi $t2, $t2, 1
        sw   $t2, -12($fp)           # j++
        j    bs_inner
bs_inner_done:
        lw   $t0, -8($fp)
        addi $t0, $t0, 1
        sw   $t0, -8($fp)            # i++
        j    bs_outer
bs_done:
        lw   $fp, -4($fp)
        addiu $sp, $sp, 16
        jr   $ra


# ---------- print_ints($a0 = a, $a1 = n) — space-separated, no trailing space ----------
# Register-based display helper: base in $t1 / n in $t8 survive the print
# syscalls; no trailing space before the first element.
print_ints:
        move $t1, $a0                # base (kept out of $a0, which syscalls use)
        move $t8, $a1                # n
        li   $t0, 0                  # i = 0
pi_loop:
        bge  $t0, $t8, pi_done
        beqz $t0, pi_print           # no space before the first element
        li   $v0, 11
        li   $a0, ' '
        syscall
pi_print:
        sll  $t2, $t0, 2
        add  $t3, $t1, $t2
        lw   $a0, 0($t3)
        li   $v0, 1                  # print_int (signed)
        syscall
        addi $t0, $t0, 1
        j    pi_loop
pi_done:
        jr   $ra
