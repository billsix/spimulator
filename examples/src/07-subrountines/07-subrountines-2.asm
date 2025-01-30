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


        .data
nl:    .asciiz     "\n"
        .text
        .globl main

mxPlusB:
        move $fp, $sp # make the stack pointer the frame pointer

        # $fp    the caller's frame pointer
        # $r0    address of return instruction of caller, automatically set by jal
        # $v0     return value
        # $a2     b
        # $a1     x
        # $a0     m

        # do mult
        mult $a1, $a0 # m * x
        mflo $v0
        # do addition
        addu $v0, $a2, $v0 # m * x + b

        jr $ra
main:
        move $fp, $sp
        addi $fp, $fp, -8

        # save onto $ra, since the evaluation of mxPlus will
        # overwrite the value

        sw $ra, 0($fp)

        # initiaze return value to a default value
        li $t0, 0
        sw $t0, 4($fp)


        # get ready to invocate y=mx+b

        move $sp, $fp

        li $a0, 1
        li $a1, 2
        li $a2, 3

        # setup for y=mx+b function is complete, jump to the address of the first instruction
        jal mxPlusB # location of continueMainPt1 is stored into $ra

continueMainPt1:

        move $sp, $fp

        # print result

        move $a0, $v0
        li $v0, 1
        syscall

        # print newline
        li $v0, 4
        la $a0, nl
        syscall


# second invoration

        move $sp, $fp

        li $a0, 4
        li $a1, 5
        li $a2, 6

        # setup for y=mx+b function is complete, jump to the address of the first instruction
        jal mxPlusB # location of continueMainPt1 is stored into $ra

continueMainPt2:

        move $sp, $fp

        # print result

        move $a0, $v0
        li $v0, 1
        syscall

        # print newline
        li $v0, 4
        la $a0, nl
        syscall





        lw $ra, 0($fp)
         ############ return the return code
        lw $v0, 4($fp)
        addi $fp, $fp, 8
        jr $ra
