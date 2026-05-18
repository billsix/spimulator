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


# C source — see 12-rev.c (line-buffered reversal of stdin)
#
#     __attribute__((noreturn)) void _start(void) {
#       int len = 0;
#       int ch = read_char();
#       while (ch != -1) {
#         if (ch == '\n') { flush_reversed(len); len = 0; }
#         else if (len < BUFSIZE) buf[len++] = ch;
#         ch = read_char();
#       }
#       if (len > 0) flush_reversed(len);
#       os_exit(0);
#     }
#
#     where `flush_reversed(len)` is:
#         for (i = len - 1; i >= 0; i--) print_char(buf[i]);
#         print_char('\n');


#PURPOSE:  Reverse each line of stdin.  A 256-byte line buffer
#          accumulates bytes until '\n' (or sentinel 'z'); then we
#          walk the buffer backwards, write each byte, write '\n',
#          and reset.
#
#NOTES:    SPIM's read_char doesn't signal EOF; we use 'z'.  Lines
#          longer than 256 bytes have the excess silently dropped.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     len           $t0                  (current line length, 0..256;
#                                          doubles as the index where
#                                          the next byte goes)
#     ch            $t1                  (most recently read byte)
#     &buf[i]       $t2                  (transient address into buf,
#                                          recomputed for each load or
#                                          store of one byte)
#     i (reverse)   $t3                  (counts down len-1..0 during
#                                          the flush_line / flush_tail
#                                          walks)
#     buf           `buf` (.data)        (256-byte line buffer)
#
#   No subroutine calls; no Cross-call saves section.
#
#   Volatile (no preserved meaning across a syscall, by convention):
#     $a0   syscall arg
#     $v0   syscall selector / read_char return
#     $t9   scratch for the `len < 256` compare (slti destination,
#           consumed by the very next branch)

        .data
buf:    .space 256                   # fixed line buffer

        .text
        .globl main
main:
        li $t0, 0                    # len = 0

read_loop:
        # ch = read_char();
        li $v0, 12
        syscall
        move $t1, $v0

        beq $t1, 'z', flush_tail     # sentinel — flush any partial line

        beq $t1, '\n', flush_line    # newline — reverse and emit

        # else if (len < 256) { buf[len] = ch; len++; }
        slti $t9, $t0, 256
        beq $t9, $0, read_loop       # buffer full — drop char
        la $t2, buf
        add $t2, $t2, $t0            # &buf[len]
        sb $t1, ($t2)                # buf[len] = ch
        addi $t0, $t0, 1
        j read_loop

flush_line:
        # Walk buf backwards: for (i = len-1; i >= 0; i--) print buf[i]
        addi $t3, $t0, -1            # i = len - 1
back_loop:
        bltz $t3, after_back         # i < 0 -> done
        la $t2, buf
        add $t2, $t2, $t3            # &buf[i]
        lb $a0, ($t2)                # load byte
        li $v0, 11                   # print_char
        syscall
        addi $t3, $t3, -1
        j back_loop
after_back:
        # print '\n'
        li $a0, '\n'
        li $v0, 11
        syscall

        li $t0, 0                    # reset len = 0
        j read_loop

flush_tail:
        # Same backward walk for any leftover bytes when sentinel
        # arrives mid-line.  If len == 0, the loop falls through.
        addi $t3, $t0, -1
tail_loop:
        bltz $t3, done
        la $t2, buf
        add $t2, $t2, $t3
        lb $a0, ($t2)
        li $v0, 11
        syscall
        addi $t3, $t3, -1
        j tail_loop

done:
        li $v0, 0
        jr $ra
