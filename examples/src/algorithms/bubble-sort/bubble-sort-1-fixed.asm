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
#           .data) in place and print it.  Just the nested-loop in-place swap.
#
#           STACK-VISIBLE STYLE: the C locals `n`, `i` (outer), and `j` (inner)
#           live in stack slots; a scratch register holds only a working COPY
#           (the two compared elements and addresses are transient).  The array
#           is file-scope data, so it stays in .data.  This demo makes NO `jal`
#           (only syscalls, which preserve $ra), so it saves $fp but not $ra.
#           Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  n           | -8($fp)     | $t9 (copy) | int       |   4   | element count (6)
#  i           | -12($fp)    | $t0 (copy) | int       |   4   | outer pass index (also the print index)
#  j           | -16($fp)    | $t2 (copy) | int       |   4   | inner comparison index
#  data[]      | data (.data)| —          | int[6]    |  24   | file-scope array sorted in place
#  (saved $fp) | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args:
#  stdout:
#  | 1
#  | 2
#  | 3
#  | 5
#  | 8
#  | 9

        .data
data:   .word 5, 2, 8, 1, 9, 3

        .text
        .globl main
main:
        addiu $sp, $sp, -16
        sw   $fp, 12($sp)            # -> -4($fp)
        addiu $fp, $sp, 16

        li   $t0, 6
        sw   $t0, -8($fp)            # n = 6
        li   $t0, 0
        sw   $t0, -12($fp)           # i = 0
outer:
        lw   $t0, -12($fp)           # i
        lw   $t9, -8($fp)            # n
        addi $t1, $t9, -1            # n - 1
        bge  $t0, $t1, outer_done    # for (i = 0; i < n-1; i++)
        li   $t0, 0
        sw   $t0, -16($fp)           # j = 0
inner:
        lw   $t2, -16($fp)           # j
        lw   $t9, -8($fp)            # n
        lw   $t0, -12($fp)           # i
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
        lw   $t2, -16($fp)
        addi $t2, $t2, 1
        sw   $t2, -16($fp)           # j++
        j    inner
inner_done:
        lw   $t0, -12($fp)
        addi $t0, $t0, 1
        sw   $t0, -12($fp)           # i++
        j    outer
outer_done:
        li   $t0, 0
        sw   $t0, -12($fp)           # reuse i's slot as the print index
print_loop:
        lw   $t0, -12($fp)
        lw   $t9, -8($fp)            # n
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
        lw   $t0, -12($fp)
        addi $t0, $t0, 1
        sw   $t0, -12($fp)
        j    print_loop
print_done:
        li   $v0, 0
        lw   $fp, -4($fp)
        addiu $sp, $sp, 16
        jr   $ra
