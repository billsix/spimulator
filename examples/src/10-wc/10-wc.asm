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


# C source — see 10-wc.c
#
#     int my_main(int argc, char **argv) {
#       int fd = STDIN;
#       if (argc > 2) usage;
#       if (argc == 2 && argv[1] != "-") fd = open(argv[1]);
#       int byte_count = 0, line_count = 0;
#       char c;
#       while (read(fd, &c, 1) > 0) {
#         byte_count++;
#         if (c == '\n') line_count++;
#       }
#       print_int(byte_count); print_string(" bytes, ");
#       print_int(line_count); print_string(" lines\n");
#       if (fd != STDIN) close(fd);
#       return 0;
#     }
#
# Invocations:
#   spimulator -f 10-wc.asm             # reads stdin
#   spimulator -f 10-wc.asm -           # reads stdin (explicit "-")
#   spimulator -f 10-wc.asm /etc/motd   # reads the file


#PURPOSE:  wc -cl with real-Unix argv handling.  No file arg (or
#          explicit "-") -> stdin.  Filename arg -> open and read.
#
#NOTES:    Switched from syscall 12 (read_char, stdin-fixed) to
#          syscall 14 (read fd into buffer) so the same loop body
#          works for both stdin and a file fd.  Loop reads 1 byte
#          at a time; a real wc would block-buffer for speed, but
#          the byte loop matches the demo's pedagogical shape.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     fd            $s1                  (0 = STDIN, else open's return)
#     argv[1]       $s4                  (saved address for error message)
#     byte_count    $s2                  (running tally)
#     line_count    $s3                  (running tally)
#     c (one byte)  `oneByte` (.data)    (1-byte read landing pad)
#
#   Cross-call saves (callee-save $s* values held LIVE across syscalls):
#     $s0   <- runtime's $ra
#     $s1   <- fd (held across every syscall in the loop)
#     $s2   <- byte_count
#     $s3   <- line_count
#     $s4   <- argv[1] address (only meaningful in the file path)
#
#   Volatile:
#     $a0..$a2  syscall args
#     $v0       syscall selector / return value

        .data
usageMsg:   .asciiz "usage: wc [FILE|-]\n"
errMsg:     .asciiz "wc: cannot open "
nlMsg:      .asciiz "\n"
bytesText:  .asciiz " bytes, "
linesText:  .asciiz " lines\n"
oneByte:    .space 1

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 0                    # fd = STDIN (default)
        move $s4, $0                 # no argv[1] yet

        # argc dispatch
        li $t0, 1
        beq $a0, $t0, read_setup     # argc == 1 -> stdin
        li $t0, 2
        bgt $a0, $t0, usage          # argc > 2 -> usage

        # argc == 2: inspect argv[1]
        lw $s4, 4($a1)               # save argv[1] address
        lb $t0, 0($s4)
        bne $t0, '-', do_open        # doesn't start with '-' -> filename
        lb $t0, 1($s4)
        beq $t0, $0, read_setup      # argv[1] is exactly "-" -> stdin

do_open:
        # fd = open(argv[1], O_RDONLY, 0)
        move $a0, $s4
        li $v0, 13                   # 13 = open
        li $a1, 0                    # O_RDONLY
        li $a2, 0                    # mode (ignored)
        syscall
        bltz $v0, open_failed
        move $s1, $v0                # save fd

read_setup:
        li $s2, 0                    # byte_count = 0
        li $s3, 0                    # line_count = 0

read_loop:
        # n = read(fd, &oneByte, 1)
        li $v0, 14                   # 14 = read
        move $a0, $s1                # fd
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, read_done          # 0 = EOF, <0 = error

        addi $s2, $s2, 1             # byte_count++
        lb $t0, oneByte
        bne $t0, '\n', read_loop
        addi $s3, $s3, 1             # line_count++
        j read_loop

read_done:
        # close fd if it's not STDIN
        beqz $s1, print_results
        li $v0, 16                   # 16 = close
        move $a0, $s1
        syscall

print_results:
        # print_int(byte_count)
        move $a0, $s2
        li $v0, 1
        syscall

        # print_string(" bytes, ")
        li $v0, 4
        la $a0, bytesText
        syscall

        # print_int(line_count)
        move $a0, $s3
        li $v0, 1
        syscall

        # print_string(" lines\n")
        li $v0, 4
        la $a0, linesText
        syscall

        move $ra, $s0
        li $v0, 0                    # exit status: __start passes this through syscall 17
        jr $ra

open_failed:
        # print "wc: cannot open <argv[1]>\n"
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
        li $v0, 17                   # exit2(1)
        syscall

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall
