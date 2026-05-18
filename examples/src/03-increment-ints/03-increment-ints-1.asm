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


# C source — see 03-increment-ints.c
#
#     __attribute__((noreturn)) void _start(void) {
#       int a, b;
#       a = b = 5;
#       print_int(++a + 5);    print_string("\n");   // pre-increment
#       print_int(a);          print_string("\n");
#       print_int(b++ + 5);    print_string("\n");   // post-increment
#       print_int(b);          print_string("\n");
#       os_exit(0);
#     }


#PURPOSE:  Demonstrate pre- and post-increment of integer variables.
#          Two locals `a` and `b` both start at 5.  Prints
#          ++a + 5, then a, then b++ + 5, then b — each on its own
#          line.
#
#VARIABLES:
#   $t0   the variable `a`  (kept in this register for the whole
#           program — no stack frame needed)
#   $t1   the variable `b`
#   $t2   scratch for (a + 5) or (b + 5) while we set up the print
#   $a0   syscall argument
#   $v0   syscall selector (1 = print_int, 4 = print_string)

        .data
nl:    .asciiz     "\n"

        .text
        .globl main
main:
        li $t0, 5                    # a = 5
        li $t1, 5                    # b = 5

        # ++a   — write back BEFORE the value is used
        addiu $t0, $t0, 1

        # print_int(a + 5);    -- with the *new* a, so this prints 11
        addiu $t2, $t0, 5            # $t2 = a + 5
        move $a0, $t2
        li $v0, 1                    # syscall 1 = print_int
        syscall

        # print_string("\n");
        li $v0, 4                    # syscall 4 = print_string
        la $a0, nl
        syscall

        # print_int(a);              -- prints 6
        move $a0, $t0
        li $v0, 1
        syscall

        # print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall

        # print_int(b + 5);          -- uses the *old* b, so prints 10
        addiu $t2, $t1, 5            # $t2 = b + 5
        move $a0, $t2
        li $v0, 1
        syscall

        # print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall

        # b++                         -- write back AFTER the previous use
        addiu $t1, $t1, 1

        # print_int(b);              -- prints 6
        move $a0, $t1
        li $v0, 1
        syscall

        # print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall

        li $v0, 0                    # exit status 0
        jr $ra                       # return to the runtime
