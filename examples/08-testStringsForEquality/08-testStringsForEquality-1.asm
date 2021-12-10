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
str1:    .asciiz     "str1"
str2:    .asciiz     "str2"
str3:    .asciiz     "str1"
        .text
        .globl main

streq:
        # $fp     the caller's frame pointer
        # $r0     address of return instruction of caller, automatically set by jal
        # $v0     return value
        # $a1     address of s2
        # $a0     address of s1
        # we don't need to use the stack to store anything,
        # so we don't need to push the frame pointer
        # nor use the stack pointer

loopBegin:
        lb $t0, ($a0)  # they are pointers, not values, so load them
                       # and we only want a byte, not a full word
        lb $t1, ($a1)
        bne $t0, $t1, loopEnd
        bne $t0, 0, incrementAndContinue
        li $v0, 0
        j str_eq_exit
incrementAndContinue:
        addi $a0, $a0, 1
        addi $a1, $a1, 1
        j loopBegin
loopEnd:
        li $v0, 1
str_eq_exit:
        jr $ra
main:
        move $fp, $sp
        addi $fp, $fp, -4

        # save onto $ra, since the evaluation of str_eq
        # overwrite the value

        sw $ra, 0($fp)

        move $sp, $fp

        # first invocation
        la $a0, str1
        la $a1, str2
        jal streq
        # print result
        move $a0, $v0
        li $v0, 1
        syscall
        # print newline
        li $v0, 4
        la $a0, nl
        syscall


        # second invocation
        la $a0, str1
        la $a1, str3
        jal streq
        # print result
        move $a0, $v0
        li $v0, 1
        syscall
        # print newline
        li $v0, 4
        la $a0, nl
        syscall

        # third invocation
        la $a0, str2
        la $a1, str3
        jal streq
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
        addi $fp, $fp, 4
        jr $ra
