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


# C source — see cat.c
#
#     int my_main(int argc, char **argv) {
#       int fd = STDIN;
#       if (argc > 2) usage;
#       if (argc == 2 && argv[1] != "-") fd = open(argv[1]);
#       char buf[4096];
#       long n;
#       while ((n = read(fd, buf, 4096)) > 0) write(STDOUT, buf, n);
#       if (fd != STDIN) close(fd);
#       return n < 0 ? 1 : 0;
#     }


# Invocations:
#   spimulator -f cat.asm             # reads stdin
#   spimulator -f cat.asm -           # reads stdin (explicit "-")
#   spimulator -f cat.asm /etc/motd   # reads the file
#   echo hello | spimulator -f cat.asm


#PURPOSE:  cat with real-Unix argv handling.  Block-at-a-time
#          I/O via syscalls 14/15.  Subsumes the old "cat-file"
#          variant.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     fd            $s1                  (STDIN=0 or opened fd)
#     argv[1]       $s4                  (for error message)
#     n             $s2                  (bytes-read from each chunk)
#     buf           `buf` (.data)        (4 KiB scratch)

        .data
usageMsg:   .asciiz "usage: cat [FILE|-]\n"
errMsg:     .asciiz "cat: cannot open "
nlMsg:      .asciiz "\n"
buf:        .space 4096

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 0                    # fd = STDIN
        move $s4, $0

        li $t0, 1
        beq $a0, $t0, read_loop
        li $t0, 2
        bgt $a0, $t0, usage

        lw $s4, 4($a1)
        lb $t0, 0($s4)
        bne $t0, '-', do_open
        lb $t0, 1($s4)
        beq $t0, $0, read_loop

do_open:
        move $a0, $s4
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        move $s1, $v0

read_loop:
        # n = read(fd, buf, 4096)
        li $v0, 14
        move $a0, $s1
        la $a1, buf
        li $a2, 4096
        syscall
        blez $v0, close_and_exit
        move $s2, $v0

        # write(STDOUT, buf, n)
        li $v0, 15
        li $a0, 1
        la $a1, buf
        move $a2, $s2
        syscall

        j read_loop

close_and_exit:
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
