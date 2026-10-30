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


# C source — see bubble-sort-1-fixed.c

# Invocation (no input; sorts a fixed array):
#   spimulator -f bubble-sort-1-fixed.asm   ->  1 2 3 5 8 9 (one per line)

# PURPOSE:  Rung 1 of the bubble-sort ladder — bubble sort a FIXED array (in
#           .data) in place and print it.  Just the nested-loop in-place swap;
#           the finished bubble-sort adds reading the ints from stdin.

# SYMBOL TABLE  (C name -> MIPS location)
#   data[] -> .word list at `data` ; n -> $t9 ; i -> $t0 ; j -> $t2

        .data
data:   .word 5, 2, 8, 1, 9, 3

        .text
        .globl main
main:
        move $s0, $ra
        li   $t9, 6                  # n = 6

        li   $t0, 0                  # i = 0
outer:
        addi $t1, $t9, -1            # n - 1
        bge  $t0, $t1, outer_done    # for (i = 0; i < n-1; i++)
        li   $t2, 0                  # j = 0
inner:
        sub  $t3, $t9, $t0
        addi $t3, $t3, -1            # n - 1 - i
        bge  $t2, $t3, inner_done    # for (j = 0; j < n-1-i; j++)
        sll  $t4, $t2, 2             # j * 4
        la   $t5, data
        add  $t5, $t5, $t4           # &data[j]
        lw   $t6, 0($t5)             # data[j]
        lw   $t7, 4($t5)             # data[j+1]
        ble  $t6, $t7, no_swap       # if (data[j] > data[j+1]) swap
        sw   $t7, 0($t5)
        sw   $t6, 4($t5)
no_swap:
        addi $t2, $t2, 1
        j    inner
inner_done:
        addi $t0, $t0, 1
        j    outer
outer_done:
        li   $t0, 0                  # print data[0..n-1]
print_loop:
        bge  $t0, $t9, print_done
        sll  $t4, $t0, 2
        la   $t5, data
        add  $t5, $t5, $t4
        lw   $a0, 0($t5)
        li   $v0, 1                  # print_int
        syscall
        li   $v0, 11                 # '\n'
        li   $a0, 10
        syscall
        addi $t0, $t0, 1
        j    print_loop
print_done:
        move $ra, $s0
        li   $v0, 0
        jr   $ra
