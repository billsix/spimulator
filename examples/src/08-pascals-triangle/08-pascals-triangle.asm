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


# C source — see 08-pascals-triangle.c
#
#     int my_main(int argc, char **argv) {
#       int rows = (argc == 2) ? parse_int(argv[1]) : 10;
#       static int row[35] = {1};
#       for (int n = 0; n < rows; n++) {
#         for (int j = n; j > 0; j--) row[j] += row[j-1];
#         for (int j = 0; j <= n; j++) { print_int(row[j]); print_char(' '); }
#         print_char('\n');
#       }
#       return 0;
#     }


#PURPOSE:  Print first N rows of Pascal's triangle.  Default N=10.
#          Static 35-cell row array (last row that fits in int32).
#
#NOTES:    Right-to-left in-place update: each row[j] = old row[j]
#          + old row[j-1].  Walking right-to-left means the new
#          row[j-1] hasn't been written yet when we read it.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     argc          $a0 at entry
#     argv          $a1 at entry
#     rows          $s1                  (from argv or 10)
#     n             $s2                  (outer loop)
#     j             $s3                  (inner loops; both update and print)
#     row           `row` (.data)        (35 .word cells; first is 1)

        .data
row:    .word 1
        .space 136                   # 34 more .word cells, zero-init (4*34 = 136)
usageMsg:   .asciiz "usage: pascals-triangle [N]   (1 <= N <= 34)\n"

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 10                   # rows = 10 (default)

        li $t0, 1
        beq $a0, $t0, run
        li $t0, 2
        bne $a0, $t0, usage

        lw $a0, 4($a1)
        jal atoi
        move $s1, $v0
        # bounds check: 1 <= rows <= 34
        blez $s1, usage
        li $t0, 34
        bgt $s1, $t0, usage

run:
        li $s2, 0                    # n = 0

outer:
        bge $s2, $s1, done

        # right-to-left update
        move $s3, $s2                # j = n

inner_update:
        blez $s3, after_update

        sll $t0, $s3, 2
        la $t1, row
        add $t1, $t1, $t0            # &row[j]
        lw $t2, 0($t1)
        lw $t3, -4($t1)
        add $t2, $t2, $t3
        sw $t2, 0($t1)

        addi $s3, $s3, -1
        j inner_update

after_update:
        # print this row
        li $s3, 0                    # j = 0

print_loop:
        bgt $s3, $s2, after_print

        sll $t0, $s3, 2
        la $t1, row
        add $t1, $t1, $t0
        lw $a0, 0($t1)
        li $v0, 1
        syscall

        li $a0, ' '
        li $v0, 11
        syscall

        addi $s3, $s3, 1
        j print_loop

after_print:
        li $a0, '\n'
        li $v0, 11
        syscall

        addi $s2, $s2, 1
        j outer

done:
        move $ra, $s0
        li $v0, 0                    # exit status: __start passes this through syscall 17
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall


# ---------- atoi subroutine -----------------------------------------
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
