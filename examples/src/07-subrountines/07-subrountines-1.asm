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

        # -------------------------------------------------------------
        # |Address in RAM  |   Value at that location                 |
        # |-----------------------------------------------------------|
        # |  20($fp)       | the caller's frame pointer               |
        # |  16($fp)       | address of return instruction of caller  |
        # |  12($fp)       | address of return value                  |
        # |  8($fp)        | b                                        |
        # |  4($fp)        | x                                        |
        # |  0($fp)        | m                                        |
        # -------------------------------------------------------------

        lw $t0, 0($fp) #m
        lw $t1, 4($fp) #x
        lw $t2, 8($fp) #b
        # do mult
        mult $t0, $t1 # m * x
        mflo $t0
        # do addition
        addu $t4, $t0, $t2 # m * x + b is in t4

        # the values in t0, t1, and t2 are no longer needed,
        # just the value in t4
        lw $t0, 12($fp) # address where caller expects to find the value of mx+b
        sw $t4, ($t0)  # write the value of mx+b at the location contained in $t0
        # the address in t0 is no longer needed
        # store the frame pointer before returning to caller
        # the address in t0 is no longer needed
        lw $t0, 16($fp)
        lw $fp, 20($fp)
        jr $t0
main:
        move $fp, $sp
        addi $fp, $fp, -12

        # initiaze result1 to a default value, not really needed
        li $t0, 0
        sw $t0, 0($fp)

        # initiaze result2 to a default value, not really needed
        li $t0, 0
        sw $t0, 4($fp)

        # initiaze return value to a default value
        li $t0, 0
        sw $t0, 8($fp)


        # get ready to invocate y=mx+b

        addi $sp, $fp, -24

        li $t0, 1
        sw $t0, 0($sp)

        li $t0, 2
        sw $t0, 4($sp)

        li $t0, 3
        sw $t0, 8($sp)

        # store address of result1
        move $t0, $fp
        addiu $t0, $t0, 0
        sw $t0, 12($sp)

        la $t0, continueMainPt1
        sw $t0, 16($sp)

        # store the frame pointer on the stack
        la $t0, ($fp)
        sw $t0, 20($sp)

        # setup for y=mx+b function is complete, jump to the address of the first instruction
        j mxPlusB

continueMainPt1:

        move $sp, $fp

        # print result

        lw $a0, 0($fp)
        li $v0, 1
        syscall

        # print newline
        li $v0, 4
        la $a0, nl
        syscall

        # second invocation of y = mx+b
        addi $sp, $fp, -24

        li $t0, 4
        sw $t0, 0($sp)

        li $t0, 5
        sw $t0, 4($sp)

        li $t0, 6
        sw $t0, 8($sp)

        # store address of result1
        move $t0, $fp
        addiu $t0, $t0, 4
        sw $t0, 12($sp)

        la $t0, continueMainPt2
        sw $t0, 16($sp)

        # store the frame pointer on the stack
        la $t0, ($fp)
        sw $t0, 20($sp)

        j mxPlusB

continueMainPt2:

        move $sp, $fp

        # print result

        lw $a0, 4($fp)
        li $v0, 1
        syscall

        # print newline
        li $v0, 4
        la $a0, nl
        syscall





        addi $fp, $fp, 12
         ############ return the return code
        lw $v0, 4($fp)
        jr $ra
