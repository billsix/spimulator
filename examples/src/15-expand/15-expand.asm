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


# C source — see 15-expand.c
#
#     int my_main(int argc, char **argv) {
#       int fd = STDIN;
#       if (argc > 2) usage;
#       if (argc == 2 && argv[1] != "-") fd = open(argv[1]);
#       int col = 0;
#       char c;
#       while (read(fd, &c, 1) > 0) {
#         if (c == '\t') { emit (8 - col%8) spaces; col += spaces; }
#         else if (c == '\n') { print_char('\n'); col = 0; }
#         else { print_char(c); col++; }
#       }
#       if (fd != STDIN) close(fd);
#       return 0;
#     }


#PURPOSE:  expand with real-Unix argv handling.  Tab width is
#          hard-coded to 8; col counter is reset to 0 on each '\n'.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     fd            $s1                  (STDIN=0 or opened fd)
#     argv[1]       $s4                  (for error message)
#     col           $s2                  (current output column)
#     c             `oneByte` (.data)    (1-byte read pad)
#     spaces        $t2                  (tab-expansion countdown)

        .data
usageMsg:   .asciiz "usage: expand [FILE|-]\n"
errMsg:     .asciiz "expand: cannot open "
nlMsg:      .asciiz "\n"
oneByte:    .space 1

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 0                    # fd = STDIN
        move $s4, $0

        li $t0, 1
        beq $a0, $t0, read_setup
        li $t0, 2
        bgt $a0, $t0, usage

        lw $s4, 4($a1)
        lb $t0, 0($s4)
        bne $t0, '-', do_open
        lb $t0, 1($s4)
        beq $t0, $0, read_setup

do_open:
        move $a0, $s4
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        move $s1, $v0

read_setup:
        li $s2, 0                    # col = 0

read_loop:
        li $v0, 14
        move $a0, $s1
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, read_done

        lb $t1, oneByte
        beq $t1, '\t', is_tab
        beq $t1, '\n', is_newline

        # default: print c, col++
        move $a0, $t1
        li $v0, 11
        syscall
        addi $s2, $s2, 1
        j read_loop

is_newline:
        li $a0, '\n'
        li $v0, 11
        syscall
        li $s2, 0
        j read_loop

is_tab:
        # spaces = 8 - (col & 7)
        andi $t2, $s2, 7
        li $a0, 8
        sub $t2, $a0, $t2
tab_loop:
        blez $t2, read_loop
        li $a0, ' '
        li $v0, 11
        syscall
        addi $s2, $s2, 1
        addi $t2, $t2, -1
        j tab_loop

read_done:
        beqz $s1, exit_ok
        li $v0, 16
        move $a0, $s1
        syscall
exit_ok:
        move $ra, $s0
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        move $a0, $s4
        li $v0, 4
        syscall
        li $v0, 4
        la $a0, nlMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall
