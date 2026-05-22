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


# C source — see print-out-ascii.c
#
#     __attribute__((noreturn)) void _start(void) {
#       char c = CHAR_MIN;             /* -128 on Linux/x86_64 */
#       print_int(c);
#       print_string("\n");
#       do {
#         c = c + 1;
#         print_int(c);
#         print_string("\n");
#       } while (c != CHAR_MAX);       /* 127 */
#       os_exit(0);
#     }


#PURPOSE:  Walk a signed value from -128 up to 127, printing each
#          on its own line.  Demonstrates a do-while loop and the
#          two's-complement representation of a negative number
#          (-128 is stored as 0xffffff80 in a 32-bit register).
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     c             $t0                  (signed counter; walked -128..127
#                                          across the do-while body)
#
#   No subroutine calls and no cross-call saves to record — $t0
#   holds `c` for the entire program.  The syscalls in between
#   iterations clobber $a0/$v0 but spim doesn't touch $t0.
#
#   Volatile (no preserved meaning across a syscall):
#     $a0   syscall arg
#     $v0   syscall selector (1 = print_int, 4 = print_string)

        .data
nl:    .asciiz     "\n"

        .text
        .globl main
main:
        li $t0, -128                 # c = CHAR_MIN  (stored as 0xffffff80)

        # Pre-loop print — emit CHAR_MIN before entering the do-while body.
        move $a0, $t0
        li $v0, 1                    # syscall 1 = print_int
        syscall
        li $v0, 4                    # syscall 4 = print_string
        la $a0, nl
        syscall

loopBegin:
        # c = c + 1;                  -- the do-while body
        add $t0, $t0, 1

        move $a0, $t0
        li $v0, 1                    # print_int(c)
        syscall
        li $v0, 4                    # print_string("\n")
        la $a0, nl
        syscall

        # } while (c != CHAR_MAX);    -- branch back while c != 127
        bne $t0, 127, loopBegin

        li $v0, 0                    # exit status 0
        jr $ra                       # return to the runtime
