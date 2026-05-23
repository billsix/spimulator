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


# C source — see helloworld.c
#
#     __attribute__((noreturn)) void _start(void) {
#       print_string("hello world\n");
#       os_exit(0);
#     }
#
# In SPIM there is no separate _start — the simulator calls `main`
# directly, so we use `main` as the entry point.  $v0 at return time
# is taken as main's exit status, which stands in for os_exit's arg.


#PURPOSE:  Print "hello world" to standard output and exit zero.
#
#VARIABLES:
#   $a0       syscall argument register
#   $v0       syscall selector; also main's return value
#               (syscall 4 = print_string;
#                see https://www.doc.ic.ac.uk/lab/secondyear/spim/node8.html)

        .data
helloworld:
        .asciiz     "hello world\n"

        .text
        .globl main
main:
        li $v0, 4                    # syscall 4 = print_string
        la $a0, helloworld           # arg = address of "hello world\n"
        syscall                      # ask the OS to print the string

        li $v0, 0                    # exit status 0
        jr $ra                       # return to the runtime
