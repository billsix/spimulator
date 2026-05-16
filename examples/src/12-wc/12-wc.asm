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


# C source — see 12-wc-1.c
#
#     __attribute__((noreturn)) void _start(void) {
#       int byte_count = 0, line_count = 0;
#       int ch = read_char();
#       while (ch != -1) {
#         byte_count++;
#         if (ch == '\n') line_count++;
#         ch = read_char();
#       }
#       print_int(byte_count);  print_string(" bytes, ");
#       print_int(line_count);  print_string(" lines\n");
#       os_exit(0);
#     }


#PURPOSE:  Count bytes and lines on stdin, print a summary.
#          Same shape as 06-commaAndPeriodCounter, generalised to a
#          real Unix utility.
#
#NOTES:    SPIM's `read_char` syscall does NOT signal EOF, so this
#          .asm uses 'z' as the terminating sentinel.  In real Unix
#          (and the C version) we use Ctrl-D / pipe-close.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     byte_count    $t0                  (running tally)
#     line_count    $t1                  (running tally, bumped on '\n')
#     ch            $t2                  (most recently read byte)
#
#   Same shape as 06-commaAndPeriodCounter — three $t-regs reused
#   across every syscall, relying on spim's preserve-$t behavior
#   rather than the formal MIPS caller-save convention.
#
#   No subroutine calls, hence no Cross-call saves section.
#
#   Volatile (no preserved meaning across a syscall, by convention):
#     $a0   syscall arg
#     $v0   syscall selector / read_char return
#           (12 = read_char, 4 = print_string, 1 = print_int)

        .data
bytesText:    .asciiz     " bytes, "
linesText:    .asciiz     " lines\n"

        .text
        .globl main
main:
        li $t0, 0                    # byte_count = 0
        li $t1, 0                    # line_count = 0

        # ch = read_char();
        li $v0, 12
        syscall
        move $t2, $v0

loop:
        beq $t2, 'z', done           # sentinel exit (no EOF in spim read_char)

        addi $t0, $t0, 1             # byte_count++
        bne $t2, '\n', not_newline
        addi $t1, $t1, 1             # line_count++
not_newline:

        # ch = read_char();
        li $v0, 12
        syscall
        move $t2, $v0
        j loop

done:
        # print_int(byte_count);
        move $a0, $t0
        li $v0, 1
        syscall

        # print_string(" bytes, ");
        li $v0, 4
        la $a0, bytesText
        syscall

        # print_int(line_count);
        move $a0, $t1
        li $v0, 1
        syscall

        # print_string(" lines\n");
        li $v0, 4
        la $a0, linesText
        syscall

        li $v0, 0
        jr $ra
