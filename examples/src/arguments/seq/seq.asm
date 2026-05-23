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


# C source — see seq.c
#
#     int my_main(int argc, char **argv) {
#       int m, n;
#       if (argc == 2)      { m = 1; n = parse_int(argv[1]); }
#       else if (argc == 3) { m = parse_int(argv[1]); n = parse_int(argv[2]); }
#       else usage;
#       for (int i = m; i <= n; i++) { print_int(i); print_char('\n'); }
#     }


# Invocations:
#   spimulator -f seq.asm 5            # 1..5
#   spimulator -f seq.asm 10 15        # 10..15
#   spimulator -f seq.asm 5 1          # 5..1 (descending)


#PURPOSE:  seq with 1- or 2-arg form.  Pure argv → loop → stdout.
#          First curriculum demo with NO input I/O syscalls at
#          all.

        .data
usageMsg:   .asciiz "usage: seq N | seq M N\n"

        .text
        .globl main
main:
        move $s0, $ra

        # 1-arg form: argc == 2
        li $t0, 2
        beq $a0, $t0, one_arg
        # 2-arg form: argc == 3
        li $t0, 3
        beq $a0, $t0, two_arg
        j usage

one_arg:
        # m = 1, n = atoi(argv[1])
        li $s1, 1
        lw $a0, 4($a1)
        jal atoi
        move $s2, $v0
        j run

two_arg:
        # m = atoi(argv[1]), n = atoi(argv[2])
        move $s3, $a1                # save argv
        lw $a0, 4($s3)
        jal atoi
        move $s1, $v0
        lw $a0, 8($s3)
        jal atoi
        move $s2, $v0

run:
        # for (i = m; i <= n; i++)
        move $t0, $s1                # i = m

loop:
        bgt $t0, $s2, done
        move $a0, $t0
        li $v0, 1                    # print_int
        syscall
        li $a0, '\n'
        li $v0, 11                   # print_char
        syscall
        addi $t0, $t0, 1
        j loop

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
