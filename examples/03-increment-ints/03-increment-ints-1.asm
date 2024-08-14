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
nl:    .asciiz     "\n"
        .text
        .globl main
main:
        ############# make the frame pointer be the stack pointer

        move $fp, $sp

        ############ frame pointer = frame_pointer - size of main stack frame
        addi $fp, $fp, -12 # subtract 3 int32_t


        ############ set a
        li $t0, 5
        sw $t0, 0($fp)

        ############ set b
        li $t0, 5
        sw $t0, 4($fp)


        ############ set return value
        li $t0, 0
        sw $t0, 8($fp)

        ############ ++a
        lw $t0, 0($fp)
        addiu $t0, $t0, 1
        sw $t0, 0($fp)


        ############ a + 5
        lw $t0, 0($fp)
        addiu $t1, $t0, 5


        ############ print a + 5
        move $a0, $t1
        li $v0, 1
        syscall

        ############ print a character for newline
        li $v0, 4
        la $a0, nl
        syscall


        ############ print a
        lw $t0, 0($fp)
        move $a0, $t0
        li $v0, 1
        syscall

        ############ print a character for newline
        li $v0, 4
        la $a0, nl
        syscall

        ############ b + 5
        lw $t0, 4($fp)
        addiu $t1, $t0, 5

        ############ print b + 5
        move $a0, $t1
        li $v0, 1
        syscall

        ############ print a character for newline
        li $v0, 4
        la $a0, nl
        syscall

        ############ b++
        lw $t0, 4($fp)
        addiu $t0, $t0, 1
        sw $t0, 4($fp)

        ############ print b
        lw $t0, 4($fp)
        move $a0, $t0
        li $v0, 1
        syscall

        ############ print a character for newline
        li $v0, 4
        la $a0, nl
        syscall

        ############ restore frame pointer
        addi $fp, $fp, 12
        ############ return the return code
        lw $v0, 8($fp)
        jr $ra
