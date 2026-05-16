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


# C source — see 02-print1through10-1.c
#
#     __attribute__((noreturn)) void _start(void) {
#       int i = 0;
#       while (i <= 10) {
#         print_int(i);
#         print_string("\n");
#         i++;
#       }
#       os_exit(0);
#     }


#PURPOSE:  Print the integers 0 through 10, each on its own line.
#
#VARIABLES:
#   $t0       loop counter i  (lives in this register for the
#               whole program — no stack frame needed)
#   $a0       syscall argument
#   $v0       syscall selector / main's return value
#               (syscall 1 = print_int, 4 = print_string)

        .data
nl:    .asciiz     "\n"

        .text
        .globl main
main:
        li $t0, 0                    # i = 0

beginningOfLoop:
        bgt $t0, 10, endOfLoop       # while (i <= 10) { ...

        # print_int(i);
        move $a0, $t0                # arg = i
        li $v0, 1                    # syscall 1 = print_int
        syscall

        # print_string("\n");
        li $v0, 4                    # syscall 4 = print_string
        la $a0, nl
        syscall

        # i++;
        addi $t0, $t0, 1
        b beginningOfLoop            # } end of while

endOfLoop:
        li $v0, 0                    # exit status 0
        jr $ra                       # return to the runtime
