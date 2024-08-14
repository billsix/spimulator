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

# NOTE --   spimulator does not support EOF, therefore use z as the terminating input



        .data
commasText:    .asciiz     " commas, "
stopsText:    .asciiz     " stops\n"
        .text
        .globl main
main:
        ############# make the frame pointer be the stack pointer
        #                                                                 current SP
        #                                                                     |
        #                                                                     |
        #                                                                     V
        #             -----------------------------------------------------------------------
        #  C-Variable |   |&comma_count|&period_count|&this_char|&return_code|           |   |
        #   ----      |---|------------|-------------|----------|------------|-----------|---|
        # RAM Address |   |-16($sp)    | -12($sp)    |  -8($sp) |  -4($sp)   | 0($sp)    |   |
        #   ----      |---|------------|-------------|----------|------------|-----------|---|
        #             |   |            |             |          |            |           |   |
        #             |   |            |             |          |            |           |   |
        #   Value     |...|dontcare    |dontcare     |dontcare  |dontcare    | dontcare  |...|
        #             |   |            |             |          |            |           |   |
        #   ____      |___|____________|_____________|__________|____________|___________|___|
        #


        move $fp, $sp

        #                                                                 current SP
        #                                                                and current FP
        #                                                                     |
        #                                                                     V
        #             ------------------------------------------------------------------------
        #  C-Variable |   |&comma_count|&period_count|&this_char|&return_code|           |   |
        #   ----      |---|------------|-------------|----------|------------|-----------|---|
        # RAM Address |   |-16($sp)    | -12($sp)    |  -8($sp) |  -4($sp)   | 0($sp)    |   |
        #   ----      |---|------------|-------------|----------|------------|-----------|---|
        #             |   |            |             |          |            |           |   |
        #             |   |            |             |          |            |           |   |
        #   Value     |...|dontcare    |dontcare     |dontcare  |dontcare    | dontcare  |...|
        #             |   |            |             |          |            |           |   |
        #   ____      |___|____________|_____________|__________|____________|___________|___|



        ############ frame pointer = frame_pointer - size of main stack frame
        addi $fp, $fp, -32 # subtract 1 char and 3 int32_t, but align the int to
                          # be at an address mod 4 = 0, so 4 * sizeof int32_t


        #                                                                 current SP
        #                   current FP                                         |
        #                       |                                              |
        #                       V                                              V
        #
        #             ------------------------------------------------------------------------
        #  C-Variable |   |&comma_count|&period_count|&this_char|&return_code|           |   |
        #   ----      |---|------------|-------------|----------|------------|-------------|---|
        # RAM Address |   | 0($fp)     |  4($fp)     |   8($fp) |  16($fp)   | 0($sp)      |   |
        #   ----      |---|------------|-------------|----------|------------|-------------|---|
        #             |   |            |             |          |            |             |   |
        #             |   |            |             |          |            |             |   |
        #   Value     |...|dontcare    |dontcare     |dontcare  |dontcare    | dontcare    |...|
        #             |   |            |             |          |            |             |   |
        #   ____      |___|____________|_____________|__________|____________|_____________|___|


        #  {
        #    int32_t comma_count_in_register = 0;
        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_COMMA_COUNT,
        #            /*src*/ &comma_count_in_register,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        #  }

        li $t0, 0
        sw $t0, 0($fp)

        #  {
        #    int32_t period_count_in_register = 0;
        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_STOP_COUNT,
        #            /*src*/ &period_count_in_register,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        #  }
        li $t0, 0
        sw $t0, 4($fp)

        #  {
        #    int32_t this_char_in_register = 0;
        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR,
        #            /*src*/ &this_char_in_register,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        #  }
        li $t0, 0
        sw $t0, 8($fp)

        #  {
        #    int32_t return_code_in_register = EXIT_SUCCESS;
        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
        #            /*src*/ &return_code_in_register,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        #  }
        #
        li $t0, 0
        sw $t0, 16($fp)

        #
        #  {
        #    char readCharacter = operating_system_read_char();
        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR,
        #            /*src*/ &readCharacter,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        #  }

        li $v0, 12
        syscall
        sw $v0, 8($fp)

        # loopBegin:
        #  {
        #    char this_char_in_register;
        #    xmemcpy(/*dest*/ &this_char_in_register,
        #            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        #    if (this_char_in_register == EOF) goto loopEnd;
        #  }
loopBegin:
        lw $t0,  8($fp)
        # spimulator does not support EOF, therefore use z as the terminating input
        beq $t0, 'z', loopEnd

        #  {
        #    char this_char_in_register;
        #    xmemcpy(/*dest*/ &this_char_in_register,
        #            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        #    if (this_char_in_register != '.') goto notAPeriod;
        #  }
        lw $t0,  8($fp)
        bne $t0, '.', notAPeriod


        #  {
        #    char period_count_in_register;
        #    xmemcpy(/*dest*/ &period_count_in_register,
        #            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_STOP_COUNT,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        lw $t0,  4($fp)

        #
        #    period_count_in_register = period_count_in_register + 1;
        #
        add $t0, $t0, 1

        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_STOP_COUNT,
        #            /*src*/ &period_count_in_register,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        sw $t0,  4($fp)

        #
        #  }
notAPeriod:
        #  {
        #    char this_char_in_register;
        #    xmemcpy(/*dest*/ &this_char_in_register,
        #            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        lw $t0,  8($fp)
        #
        #    if (this_char_in_register != ',')  goto notAComma;
        bne $t0, ',', notAComma

        #  }
        #  {
        #    char comma_count_in_register;
        #    xmemcpy(/*dest*/ &comma_count_in_register,
        #            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_COMMA_COUNT,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        lw $t0,  0($fp)

        #
        #    comma_count_in_register = comma_count_in_register + 1;
        #
        add $t0, $t0, 1
        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_COMMA_COUNT,
        #            /*src*/ &comma_count_in_register,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        sw $t0,  0($fp)
        #  }
notAComma:
        #  {
        #    char readCharacter = operating_system_read_char();
        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR,
        #            /*src*/ &readCharacter,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);

        li $v0, 12
        syscall
        sw $v0, 8($fp)


        #  goto loopBegin;
        j loopBegin
        #  }
        #
        #
loopEnd:
        #  {
        #    char comma_count_in_register;
        #    xmemcpy(/*dest*/ &comma_count_in_register,
        #            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_COMMA_COUNT,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        lw $t0,  0($fp)


        #    operating_system_print_int(comma_count_in_register);
        #  }
        move $a0, $t0
        li $v0, 1
        syscall

        #  operating_system_print_string(" commas, ");

        li $v0, 4
        la $a0, commasText
        syscall

        #  {
        #    char period_count_in_register;
        #    xmemcpy(/*dest*/ &period_count_in_register,
        #            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_STOP_COUNT,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        lw $t0, 4($fp)
        #    operating_system_print_int(period_count_in_register);
        move $a0, $t0
        li $v0, 1
        syscall


        #  }
        #  operating_system_print_string(" stops\n");

        li $v0, 4
        la $a0, stopsText
        syscall

        #  {
        #    char return_code_in_register;
        #    xmemcpy(/*dest*/ &return_code_in_register,
        #            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        lw $v0, 16($fp)

        addi $fp, $fp, 32
        #    return return_code_in_register;
        jr $ra
        #  }
        #
