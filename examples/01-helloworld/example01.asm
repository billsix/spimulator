# Copyright (c) 2021 William Emerison Six
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
#
#


# for reference on system calls, look at
# https://www.doc.ic.ac.uk/lab/secondyear/spim/node8.html

        .data
helloworld:
        .asciiz     "hello world\n"
        .text
        .globl main
main:
        # make the frame pointer be the stack pointer
        move $fp, $sp
        # frame pointer = frame_pointer - size of main stack frame
        addi $fp, $fp, -12 # subtract 2 int32_t and one pointer, each of which are 4 bytes
        # s0-8 are free to use, as are t0-8

        # $a0 = argc, $a1 = argv
        # 4($a1) is first command line argv 8($a1) is second
        # save argc onto the stack
        sw $a0, 0($fp)  # now stackframe argc can go on the stack,
        # save argv onto the stack
        sw $a1, 4($fp)  # now stackframe argv can go on the stack,

        # print a hello world
        li $v0, 4
        la $a0, helloworld
        syscall

        jr $ra
