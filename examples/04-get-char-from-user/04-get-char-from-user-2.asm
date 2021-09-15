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
commaSpaceValue:    .asciiz     ", value "
chWas:    .asciiz     "ch was "
        .text
        .globl main
main:
        ############# make the frame pointer be the stack pointer

        #                                                  current SP
        #                                                      |
        #                                                      |
        #                                                      V
        #   RAM
        #   ----      |---|----------|----------|----------|-----------|---|
        #  Address    |   |   --     |  -8$(sp) |  -4$(sp) | 0$(sp)    |   |
        #   ----      |---|----------|----------|----------|-----------|---|
        #             |   |          |          |          |           |   |
        #             |   |          |          |          |           |   |
        #   Value     |...|   --     |dontcare  |dontcare  | dontcare  |...|
        #             |   |          |          |          |           |   |
        #   ____      |___|__________|__________|__________|___________|___|
        #
        #  C-Variable                return_code    ch
        #


        move $fp, $sp

        #                                                  current SP
        #                                                 and current FP
        #                                                      |
        #                                                      V
        #   RAM
        #   ----      |---|----------|----------|----------|-----------|---|
        #  Address    |   |   --     |  -8$(sp) |  -4$(sp) | 0$(sp)    |   |
        #   ----      |---|----------|----------|----------|-----------|---|
        #             |   |          |          |          |           |   |
        #             |   |          |          |          |           |   |
        #   Value     |...|   --     |dontcare  |dontcare  | dontcare  |...|
        #             |   |          |          |          |           |   |
        #   ____      |___|__________|__________|__________|___________|___|
        #
        #  C-Variable                    ch      return_code
        #



        ############ frame pointer = frame_pointer - size of main stack frame
        addi $fp, $fp, 8 # subtract 1 char and 1 int32_t, but align the int to
                         # be at an address mod 4 = 0


        #                                                  current SP
        #                             current FP               |
        #                                 |                    |
        #                                 V                    V
        #   RAM
        #   ----      |---|----------|----------|----------|-----------|---|
        #  Address    |   |   --     |   0$(fp) |  4$(fp)  | 0$(sp)    |   |
        #   ----      |---|----------|----------|----------|-----------|---|
        #             |   |          |          |          |           |   |
        #             |   |          |          |          |           |   |
        #   Value     |...|   --     |dontcare  |dontcare  | dontcare  |...|
        #             |   |          |          |          |           |   |
        #   ____      |___|__________|__________|__________|___________|___|
        #
        #  C-Variable                    ch      return_code
        #


        ############ set ch
        li $t0, 0
        sw $t0, 0($fp)

        #                                                  current SP
        #                             current FP               |
        #                                 |                    |
        #                                 V                    V
        #   RAM
        #   ----      |---|----------|----------|----------|-----------|---|
        #  Address    |   |   --     |   0$(fp) |  4$(fp)  | 0$(sp)    |   |
        #   ----      |---|----------|----------|----------|-----------|---|
        #             |   |          |          |          |           |   |
        #             |   |          |          |          |           |   |
        #   Value     |...|   --     |0x00000000|dontcare  | dontcare  |...|
        #             |   |          |          |          |           |   |
        #   ____      |___|__________|__________|__________|___________|___|
        #
        #  C-Variable                    ch      return_code
        #



        ############ set return_value
        li $t0, 0
        sw $t0, 4($fp)

        #                                                  current SP
        #                             current FP               |
        #                                 |                    |
        #                                 V                    V
        #   RAM
        #   ----      |---|----------|----------|----------|-----------|---|
        #  Address    |   |   --     |   0$(fp) |  4$(fp)  | 0$(sp)    |   |
        #   ----      |---|----------|----------|----------|-----------|---|
        #             |   |          |          |          |           |   |
        #             |   |          |          |          |           |   |
        #   Value     |...|   --     |0x00000000|0x00000000| dontcare  |...|
        #             |   |          |          |          |           |   |
        #   ____      |___|__________|__________|__________|___________|___|
        #
        #  C-Variable                    ch      return_code
        #



        # read char, which will end up in $v0, and store it in 0($fp)
        li $v0 12
        syscall

        sw $v0, 0($fp)
        #                                                  current SP
        #                             current FP               |
        #                                 |                    |
        #                                 V                    V
        #   RAM
        #   ----      |---|----------|----------|----------|-----------|---|
        #  Address    |   |   --     |   0$(fp) |  4$(fp)  | 0$(sp)    |   |
        #   ----      |---|----------|----------|----------|-----------|---|
        #             |   |          |          |          |           |   |
        #             |   |          |character |          |           |   |
        #   Value     |...|   --     | input by |0x00000000| dontcare  |...|
        #             |   |          |  user    |          |           |   |
        #   ____      |___|__________|__________|__________|___________|___|
        #
        #  C-Variable                    ch      return_code
        #



loopTest:
        # if (!(ch_in_register != 'a'))
        #   goto loopEnd;

        lw $t0, 0($fp)
        beq $t0, 'a', loopEnd

loopBody:
        # if (!(ch_in_register != '\n'))
        #   goto getNextChar;

        lw $t0, 0($fp)
        beq $t0, '\n', getNextChar

        # print_string("ch was ");
        li $v0, 4
        la $a0, chWas
        syscall

        #     print_char(ch_in_register);
        lw $t0, 0($fp)
        move $a0, $t0
        li $v0, 1
        syscall


        #   print_string(", value ");
        li $v0, 4
        la $a0, commaSpaceValue
        syscall

        #     print_int(ch_in_register);
        lw $t0, 0($fp)
        li $v0, 11
        move $a0, $t0
        syscall

        #   print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall


getNextChar:
        # read char, which will end up in $v0
        li $v0 12
        syscall

        sw $v0, 0($fp)
        #                                                  current SP
        #                             current FP               |
        #                                 |                    |
        #                                 V                    V
        #   RAM
        #   ----      |---|----------|----------|----------|-----------|---|
        #  Address    |   |   --     |   0$(fp) |  4$(fp)  | 0$(sp)    |   |
        #   ----      |---|----------|----------|----------|-----------|---|
        #             |   |          |  new     |          |           |   |
        #             |   |          |character |          |           |   |
        #   Value     |...|   --     | input by |0x00000000| dontcare  |...|
        #             |   |          |  user    |          |           |   |
        #   ____      |___|__________|__________|__________|___________|___|
        #
        #  C-Variable                    ch      return_code
        #


        j loopTest

loopEnd:


        ############ return the return code
        lw $v0, 4($fp)
        jr $ra
