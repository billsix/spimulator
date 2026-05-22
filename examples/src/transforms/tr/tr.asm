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


# C source — see tr.c
#
#     __attribute__((noreturn)) void _start(void) {
#       int ch = read_char();
#       while (ch != -1) {
#         if (ch >= 'a' && ch <= 'z')
#           ch = ch - 32;
#         print_char((char)ch);
#         ch = read_char();
#       }
#       os_exit(0);
#     }


#PURPOSE:  Copy stdin to stdout, upcasing every lowercase ASCII
#          byte.  Demonstrates byte-level conditional transformation:
#          read a byte, check whether it falls in a range, optionally
#          mutate it, write it.
#
#NOTES:    `read_char` returns -1 at EOF.  The loop branches on
#          `bltz $t0, done` — no sentinel character is needed.
#
#VARIABLES:
#   $t0   current char
#   $a0   syscall arg
#   $v0   syscall selector / read result
#           (12 = read_char, 11 = print_char)

        .text
        .globl main
main:
loop:
        # ch = read_char();
        li $v0, 12
        syscall
        move $t0, $v0

        bltz $t0, done               # -1 -> EOF

        # if (ch < 'a') goto write;          // skip lower bound
        blt $t0, 'a', write
        # if (ch > 'z') goto write;          // skip upper bound
        bgt $t0, 'z', write
        # ch = ch - 32;
        addi $t0, $t0, -32

write:
        # print_char(ch);
        move $a0, $t0
        li $v0, 11
        syscall

        j loop

done:
        li $v0, 0
        jr $ra
