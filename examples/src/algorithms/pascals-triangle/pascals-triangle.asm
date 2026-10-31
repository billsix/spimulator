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


# C source — see pascals-triangle.c


# Invocations:
#   spimulator -f pascals-triangle.asm        # 10 rows (default)
#   spimulator -f pascals-triangle.asm 6      # 6 rows


#PURPOSE:  Print first N rows of Pascal's triangle.  Default N=10.  Right-to-left
#          in-place row update: each row[j] = old row[j] + old row[j-1].
#
#          STACK-VISIBLE STYLE: the C locals `rows`, the outer counter `n`, and
#          the inner counter `j` live in stack slots; a scratch register holds
#          only a working COPY (element addresses are transient).  The row array
#          is file-scope data, so it stays in .data.  main makes `jal atoi`, so
#          it saves $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  rows        | -12($fp)    | $t1 (copy) | int       |   4   | number of rows (argv[1] or 10)
#  n           | -16($fp)    | $t0 (copy) | int       |   4   | outer loop: current row index
#  j           | -20($fp)    | $t0 (copy) | int       |   4   | inner loop: update + print index
#  row[]       | row (.data) | —          | int[35]   | 140   | file-scope row buffer (first cell is 1)
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | jal atoi
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#  No #CANONICAL block: each row line ends with a trailing space, which a
#  comment-based anchor can't carry.  Gated by before/after output equivalence.

        .data
row:    .word 1
        .space 136                   # 34 more .word cells, zero-init (4*34 = 136)
usageMsg:   .asciiz "usage: pascals-triangle [N]   (1 <= N <= 34)\n"

        .text
        .globl main
main:
        addiu $sp, $sp, -24
        sw $ra, 20($sp)              # -> -4($fp)
        sw $fp, 16($sp)              # -> -8($fp)
        addiu $fp, $sp, 24

        li $t0, 10
        sw $t0, -12($fp)             # rows = 10 (default)

        li $t0, 1
        beq $a0, $t0, run
        li $t0, 2
        bne $a0, $t0, usage
        lw $a0, 4($a1)
        jal atoi
        sw $v0, -12($fp)             # rows = atoi(argv[1])
        lw $t0, -12($fp)
        blez $t0, usage              # 1 <= rows <= 34
        li $t1, 34
        bgt $t0, $t1, usage

run:
        li $t0, 0
        sw $t0, -16($fp)             # n = 0

outer:
        lw $t0, -16($fp)             # n
        lw $t1, -12($fp)             # rows
        bge $t0, $t1, done

        # right-to-left update: j = n
        lw $t0, -16($fp)
        sw $t0, -20($fp)             # j = n
inner_update:
        lw $t0, -20($fp)             # j
        blez $t0, after_update
        sll $t1, $t0, 2
        la $t2, row
        add $t2, $t2, $t1            # &row[j]
        lw $t3, 0($t2)
        lw $t4, -4($t2)
        add $t3, $t3, $t4
        sw $t3, 0($t2)               # row[j] += row[j-1]
        lw $t0, -20($fp)
        addi $t0, $t0, -1
        sw $t0, -20($fp)             # j--
        j inner_update

after_update:
        # print this row
        li $t0, 0
        sw $t0, -20($fp)             # j = 0
print_loop:
        lw $t0, -20($fp)             # j
        lw $t1, -16($fp)             # n
        bgt $t0, $t1, after_print
        sll $t1, $t0, 2
        la $t2, row
        add $t2, $t2, $t1
        lw $a0, 0($t2)               # row[j]
        li $v0, 1
        syscall
        li $a0, ' '
        li $v0, 11
        syscall
        lw $t0, -20($fp)
        addi $t0, $t0, 1
        sw $t0, -20($fp)             # j++
        j print_loop

after_print:
        li $a0, '\n'
        li $v0, 11
        syscall
        lw $t0, -16($fp)
        addi $t0, $t0, 1
        sw $t0, -16($fp)             # n++
        j outer

done:
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
