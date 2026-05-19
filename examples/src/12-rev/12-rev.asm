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


# C source — see 12-rev.c
#
#     int my_main(int argc, char **argv) {
#       int fd = STDIN;
#       if (argc > 2) usage;
#       if (argc == 2 && argv[1] != "-") fd = open(argv[1]);
#       int len = 0;
#       char c;
#       while (read(fd, &c, 1) > 0) {
#         if (c == '\n') { flush_reversed(len); len = 0; }
#         else if (len < 256) buf[len++] = c;
#       }
#       if (len > 0) flush_reversed(len);
#       if (fd != STDIN) close(fd);
#       return 0;
#     }


#PURPOSE:  rev with real-Unix argv handling.  Buffers each line
#          in `buf` (256 bytes), then walks it backwards on
#          newline.  Input source picked from argv as in 10-wc.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     fd            $s1                  (STDIN=0 or opened fd)
#     argv[1]       $s4                  (for error message)
#     len           $s2                  (current line length, 0..256)
#     buf           `buf` (.data)        (256-byte line buffer)
#     c             `oneByte` (.data)    (1-byte read pad)
#
#   Cross-call saves:
#     $s0   <- runtime's $ra
#     $s1   <- fd
#     $s2   <- len
#     $s4   <- argv[1] address

        .data
usageMsg:   .asciiz "usage: rev [FILE|-]\n"
errMsg:     .asciiz "rev: cannot open "
nlMsg:      .asciiz "\n"
buf:        .space 256
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
        li $s2, 0                    # len = 0

read_loop:
        li $v0, 14
        move $a0, $s1
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, read_done          # EOF

        lb $t0, oneByte
        beq $t0, '\n', flush_line

        # else if (len < 256) buf[len++] = c
        li $t1, 256
        bge $s2, $t1, read_loop      # buffer full -> drop byte
        la $t1, buf
        add $t1, $t1, $s2            # &buf[len]
        sb $t0, ($t1)
        addi $s2, $s2, 1
        j read_loop

flush_line:
        # walk buf backwards
        addi $t3, $s2, -1            # i = len - 1
back_loop:
        bltz $t3, after_back
        la $t1, buf
        add $t1, $t1, $t3
        lb $a0, ($t1)
        li $v0, 11
        syscall
        addi $t3, $t3, -1
        j back_loop
after_back:
        li $a0, '\n'
        li $v0, 11
        syscall
        li $s2, 0
        j read_loop

read_done:
        # flush trailing partial line (no newline at end of input)
        blez $s2, after_tail
        addi $t3, $s2, -1
tail_loop:
        bltz $t3, after_tail
        la $t1, buf
        add $t1, $t1, $t3
        lb $a0, ($t1)
        li $v0, 11
        syscall
        addi $t3, $t3, -1
        j tail_loop
after_tail:
        # close fd if not STDIN
        beqz $s1, exit_ok
        li $v0, 16
        move $a0, $s1
        syscall
exit_ok:
        move $ra, $s0
        li $v0, 0                    # exit status: __start passes this through syscall 17
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
