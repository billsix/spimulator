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
a:    .asciiz     "a"
nl:    .asciiz     "\n"
commaSpaceValue:    .asciiz     ", value "
chWas:    .asciiz     "ch was "
        .text
        .globl main
main:

        #### NOTE - this code does not work when run "spimulator -f 04-get-char-from-user/04-get-char-from-user-1.asm"
        ####  THIS IS BECAUSE INTEGERS NEED TO BE ALIGNED ON 32 BIT BOUNDARIES!!!!!
        ####  THE FIX IS IN 04-get-char-from-user/04-get-char-from-user-2.asm


        ############# make the frame pointer be the stack pointer

        move $fp, $sp

        ############ frame pointer = frame_pointer - size of main stack frame
        addi $fp, $fp, -5 # subtract 1 char and 1 int32_t


        ############ set ch
        li $t0, 0
        sw $t0, 0($fp)

        ############ set return_value
        li $t0, 0
        sw $t0, 1($fp)

        # read char, which will end up in $v0
        li $v0 12
        syscall

loopTest:
        # if (!(ch_in_register != 'a'))
        #   goto loopEnd;

        la $t0, a
        beq $v0, $t0, loopEnd

        # if (!(ch_in_register != '\n'))
        #   goto getNextChar;

        la $t0, nl
        beq $v0, $t0, getNextChar

        # operating_system_print_string("ch was ");
        li $v0, 4
        la $a0, chWas
        syscall

        #     operating_system_print_char(ch_in_register);
        move $a0, $v0
        li $v0, 1
        syscall


        #   operating_system_print_string(", value ");
        li $v0, 4
        la $a0, commaSpaceValue
        syscall

        # read char, which will end up in $v0
        li $v0 12
        syscall

        j loopTest

loopEnd:
        #   operating_system_print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall

getNextChar:


        ############ return the return code
        lw $v0, 8($fp)
        addi $fp, $fp, 5
        jr $ra
