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


# C source — see bubble-sort.c
#
#     int my_main(int argc, char **argv) {
#       int data[256];
#       int n = 0;
#       int value;
#       while (n < 256 && read_int_from_stdin(&value) == 0) data[n++] = value;
#       bubble_sort(data, n);
#       for (int i = 0; i < n; i++) { print_int(data[i]); print_char('\n'); }
#     }


#PURPOSE:  Bubble sort N ints read from stdin.  N up to 256.
#          First demo to use syscall 5 (read_int) with the
#          $a3 EOF flag.  Output is one int per line, sorted
#          ascending — same shape as `sort -n` for small inputs.
#
#NOTES:    spim's read_int (syscall 5):
#            $v0 = parsed int  (0 on EOF, but $a3 is the truth)
#            $a3 = 0 on success, 1 on EOF
#          We branch on $a3 to know when to stop reading.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     n             $s1                  (count of ints read so far)
#     data          `data` (.data)       (256 .word cells, BSS-zeroed)
#     value         $v0 (transient)      (each read_int's return)
#
#   In bubble_sort: same shape as before — $s2=i, $s3=j, etc.,
#   but here main+bubble_sort+print all live in one function so
#   we just use $t-regs for the sort body.

        .data
data:       .space 1024                  # 256 ints * 4 bytes
usageMsg:   .asciiz "usage: bubble-sort   (reads ints from stdin)\n"

        .text
        .globl main
main:
        move $s0, $ra

        # if (argc != 1) usage
        li $t0, 1
        bne $a0, $t0, usage

        # Read ints from stdin until EOF.
        li $s1, 0                    # n = 0

read_loop:
        li $t0, 256
        bge $s1, $t0, after_read     # cap at 256

        li $v0, 5                    # syscall 5 = read_int
        syscall
        bnez $a3, after_read         # $a3 = 1 -> EOF

        # data[n++] = $v0
        sll $t0, $s1, 2
        la $t1, data
        add $t1, $t1, $t0
        sw $v0, ($t1)
        addi $s1, $s1, 1
        j read_loop

after_read:
        # bubble_sort(data, n)
        # Outer: i = 0..n-2
        li $t0, 0                    # i
        addi $t1, $s1, -1            # n - 1

outer:
        bge $t0, $t1, after_sort

        sub $t2, $t1, $t0            # n - 1 - i
        li $t3, 0                    # j = 0

inner:
        bge $t3, $t2, next_outer

        # &a[j]
        sll $t4, $t3, 2
        la $t5, data
        add $t5, $t5, $t4
        lw $t6, 0($t5)               # a[j]
        lw $t7, 4($t5)               # a[j+1]
        ble $t6, $t7, no_swap
        sw $t7, 0($t5)
        sw $t6, 4($t5)
no_swap:
        addi $t3, $t3, 1
        j inner

next_outer:
        addi $t0, $t0, 1
        j outer

after_sort:
        # Print: for (i = 0; i < n; i++) print_int(data[i]); print_char('\n');
        li $t0, 0                    # i

print_loop:
        bge $t0, $s1, print_done

        sll $t1, $t0, 2
        la $t2, data
        add $t2, $t2, $t1
        lw $a0, ($t2)
        li $v0, 1                    # print_int
        syscall

        li $a0, '\n'
        li $v0, 11                   # print_char
        syscall

        addi $t0, $t0, 1
        j print_loop

print_done:
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
