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

# PURPOSE:  Unit tests for the bubble-sort core.  bubble_sort($a0=a, $a1=n) sorts
#           an array in place (no I/O), so it can be driven by a table of
#           hardcoded arrays.  main loops the cases, printing "<input> => <sorted>"
#           (print the array, sort, print it again); run-demo.sh pins the golden.
#           bubble_sort is bubble-sort.asm's, verbatim.

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
        move $s0, $ra
        li   $s1, 0                  # i = 0
        li   $s2, 7                  # case_count
mt_loop:
        bge  $s1, $s2, mt_done
        sll  $t0, $s1, 2
        la   $t1, cases
        add  $t1, $t1, $t0
        lw   $s3, 0($t1)             # arr ptr
        la   $t1, lens
        add  $t1, $t1, $t0
        lw   $s4, 0($t1)             # n
        move $a0, $s3                # print the input
        move $a1, $s4
        jal  print_ints
        la   $a0, arrow              # " => "
        li   $v0, 4
        syscall
        move $a0, $s3                # sort in place
        move $a1, $s4
        jal  bubble_sort
        move $a0, $s3                # print the sorted array
        move $a1, $s4
        jal  print_ints
        li   $v0, 11                 # newline
        li   $a0, 10
        syscall
        addi $s1, $s1, 1
        j    mt_loop
mt_done:
        move $ra, $s0
        li   $v0, 0
        jr   $ra


# ---------- bubble_sort($a0 = a, $a1 = n) — in place (bubble-sort.asm, verbatim) ----------
bubble_sort:
        li   $t0, 0                  # i = 0
bs_outer:
        addi $t1, $a1, -1            # n - 1
        bge  $t0, $t1, bs_done       # for (i = 0; i < n-1; i++)
        li   $t2, 0                  # j = 0
bs_inner:
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
        addi $t2, $t2, 1
        j    bs_inner
bs_inner_done:
        addi $t0, $t0, 1
        j    bs_outer
bs_done:
        jr   $ra


# ---------- print_ints($a0 = a, $a1 = n) — space-separated, no trailing space ----------
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
