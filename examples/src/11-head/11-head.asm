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


# C source — see 11-head.c
#
#     #define N 10
#
#     __attribute__((noreturn)) void _start(void) {
#       int line_count = 0;
#       int ch = read_char();
#       while (ch != -1) {
#         print_char((char)ch);
#         if (ch == '\n') {
#           line_count++;
#           if (line_count == N) break;
#         }
#         ch = read_char();
#       }
#       os_exit(0);
#     }


#PURPOSE:  Echo the first 10 lines of stdin to stdout, then stop.
#          N is hardcoded — argv plumbing isn't available in spim.
#
#NOTES:    SPIM's `read_char` syscall doesn't signal EOF, so we
#          also accept 'z' as a terminator.  In real Unix the EOF
#          (n <= 0 from os_read) is the only stop signal.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     line_count    $t0                  (0..N, where N=10 hardcoded)
#     ch            $t1                  (most recently read byte)
#
#   Two $t-regs reused across syscalls per the same spim-
#   preserves-$t convention as 06 and 12.
#
#   Volatile (no preserved meaning across a syscall, by convention):
#     $a0   syscall arg
#     $v0   syscall selector / read_char return
#           (12 = read_char, 11 = print_char)

        .text
        .globl main
main:
        li $t0, 0                    # line_count = 0

loop:
        # ch = read_char();
        li $v0, 12
        syscall
        move $t1, $v0

        beq $t1, 'z', done           # sentinel exit

        # print_char(ch);
        move $a0, $t1
        li $v0, 11
        syscall

        # if (ch == '\n') { line_count++; if (line_count == 10) break; }
        bne $t1, '\n', loop          # not a newline -> next iteration
        addi $t0, $t0, 1
        bne $t0, 10, loop            # haven't hit N yet -> next iteration

done:
        li $v0, 0
        jr $ra
