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

        #                                                    current SP
        #                                                        |
        #                                                        |
        #                                                        V
        #
        #             --------------------------------------------------------
        #  C-Variable |   |          |   &ch    |&return_code|           |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        # RAM Address |   |   --     |  -8($sp) |  -4($sp)   | 0($sp)    |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        #             |   |          |          |            |           |   |
        #             |   |          |          |            |           |   |
        #   Value     |...|   --     |dontcare  |dontcare    | dontcare  |...|
        #             |   |          |          |            |           |   |
        #   ____      |___|__________|__________|____________|___________|___|
        #
        #


        move $fp, $sp

        #                                                    current SP
        #                                                   and current FP
        #                                                        |
        #                                                        V
        #
        #             --------------------------------------------------------
        #  C-Variable |   |          |   &ch    |&return_code|           |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        # RAM Address |   |   --     |  -8($sp) |  -4($sp)   | 0($sp)    |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        #             |   |          |          |            |           |   |
        #             |   |          |          |            |           |   |
        #   Value     |...|   --     |dontcare  |dontcare    | dontcare  |...|
        #             |   |          |          |            |           |   |
        #   ____      |___|__________|__________|____________|___________|___|
        #
        #


        # frame_pointer = frame_pointer - SIZE_OF_MAIN_STACK_FRAME;


        ############ frame pointer = frame_pointer - size of main stack frame
        addi $fp, $fp, -8 # subtract 1 char and 1 int32_t, but align the int to
                          # be at an address mod 4 = 0


        #                                                    current SP
        #                               current FP               |
        #                                   |                    |
        #                                   V                    V
        #
        #             --------------------------------------------------------
        #  C-Variable |   |          |   &ch    |&return_code|           |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        # RAM Address |   |   --     |   0($fp) |  4($fp)    | 0($sp)    |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        #             |   |          |          |            |           |   |
        #             |   |          |          |            |           |   |
        #   Value     |...|   --     |dontcare  |dontcare    | dontcare  |...|
        #             |   |          |          |            |           |   |
        #   ____      |___|__________|__________|____________|___________|___|
        #
        #


        #  {
        #    int32_t ch_in_register = 0;
        li $t0, 0
        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
        #            /*src*/ &ch_in_register,
        #            /*numberOfBytes*/ SIZE_OF_BYTE);
        ############ set ch
        sw $t0, 0($fp)



        #                                                    current SP
        #                               current FP               |
        #                                   |                    |
        #                                   V                    V
        #
        #             --------------------------------------------------------
        #  C-Variable |   |          |   &ch    |&return_code|           |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        # RAM Address |   |   --     |   0($fp) |  4($fp)    | 0($sp)    |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        #             |   |          |          |            |           |   |
        #             |   |          |          |            |           |   |
        #   Value     |...|   --     |0x00000000|dontcare    | dontcare  |...|
        #             |   |          |          |            |           |   |
        #   ____      |___|__________|__________|____________|___________|___|
        #
        #



        ############ set return_value
        #    int32_t return_code_in_register = 0;
        li $t0, 0
        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
        #            /*src*/ &return_code_in_register,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        #  }
        sw $t0, 4($fp)

        #                                                    current SP
        #                               current FP               |
        #                                   |                    |
        #                                   V                    V
        #
        #             --------------------------------------------------------
        #  C-Variable |   |          |   &ch    |&return_code|           |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        # RAM Address |   |   --     |   0($fp) |  4($fp)    | 0($sp)    |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        #             |   |          |          |            |           |   |
        #             |   |          |          |            |           |   |
        #   Value     |...|   --     |0x00000000|0x00000000  | dontcare  |...|
        #             |   |          |          |            |           |   |
        #   ____      |___|__________|__________|____________|___________|___|
        #
        #




        #
        #  {
        #    // main_stack_frame.ch = operating_system_read_char();
        #    char ch_in_register = operating_system_read_char();

        # read char, which will end up in $v0, and store it in 0($fp)
        li $v0 12
        syscall

        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
        #            /*src*/ &ch_in_register,
        #            /*numberOfBytes*/ SIZE_OF_BYTE);
        #  }

        sw $v0, 0($fp)
        #                                                    current SP
        #                               current FP               |
        #                                   |                    |
        #                                   V                    V
        #
        #             --------------------------------------------------------
        #  C-Variable |   |          |   &ch    |&return_code|           |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        # RAM Address |   |   --     |   0($fp) |  4($fp)    | 0($sp)    |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        #             |   |          |          |            |           |   |
        #             |   |          |character |            |           |   |
        #   Value     |...|   --     | input by |0x00000000  | dontcare  |...|
        #             |   |          |  user    |            |           |   |
        #   ____      |___|__________|__________|____________|___________|___|
        #
        #


        #loopTest : {

loopTest:
        #  char ch_in_register;
        #  xmemcpy(/*dest*/ &ch_in_register,
        #          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
        #          /*numberOfBytes*/ SIZE_OF_BYTE);
        lw $t0, 0($fp)
        #  if (!(ch_in_register != 'a'))
        #    goto loopEnd;
        #}
        beq $t0, 'a', loopEnd


        #loopBody : {
loopBody:
        #   goto getNextChar;

        #  char ch_in_register;
        #  xmemcpy(/*dest*/ &ch_in_register,
        #          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
        #          /*numberOfBytes*/ SIZE_OF_BYTE);
        lw $t0, 0($fp)
        #  if (!(ch_in_register != '\n'))
        #    goto getNextChar;
        #}
        beq $t0, '\n', getNextChar

        # operating_system_print_string("ch was ");
        li $v0, 4
        la $a0, chWas
        syscall

        #  {
        #    char ch_in_register;
        #    xmemcpy(/*dest*/ &ch_in_register,
        #            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
        #            /*numberOfBytes*/ SIZE_OF_BYTE);
        lw $t0, 0($fp)
        #    operating_system_print_char(ch_in_register);
        move $a0, $t0
        li $v0, 1
        syscall
        #  }


        #   operating_system_print_string(", value ");
        li $v0, 4
        la $a0, commaSpaceValue
        syscall

        #  {
        #    char ch_in_register;
        #    xmemcpy(/*dest*/ &ch_in_register,
        #            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
        #            /*numberOfBytes*/ SIZE_OF_BYTE);
        lw $t0, 0($fp)
        #    operating_system_print_int(ch_in_register);
        li $v0, 11
        move $a0, $t0
        syscall
        #  }

        #   operating_system_print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall

        #getNextChar : {
getNextChar:
        #  char ch_in_register = operating_system_read_char();
        # read char, which will end up in $v0
        li $v0 12
        syscall

        #  xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
        #          /*src*/ &ch_in_register,
        #          /*numberOfBytes*/ SIZE_OF_BYTE);
        sw $v0, 0($fp)


        #}

        #                                                    current SP
        #                               current FP               |
        #                                   |                    |
        #                                   V                    V
        #
        #             --------------------------------------------------------
        #  C-Variable |   |          |   &ch    |&return_code|           |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        # RAM Address |   |   --     |   0($fp) |  4($fp)    | 0($sp)    |   |
        #   ----      |---|----------|----------|------------|-----------|---|
        #             |   |          |  new     |            |           |   |
        #             |   |          |character |            |           |   |
        #   Value     |...|   --     | input by |0x00000000  | dontcare  |...|
        #             |   |          |  user    |            |           |   |
        #   ____      |___|__________|__________|____________|___________|___|
        #
        #


        #  goto loopTest;
        j loopTest

loopEnd:
        #loopEnd : {
        #  int32_t return_code_in_register;
        #  xmemcpy(/*dest*/ &return_code_in_register,
        #          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
        #          /*numberOfBytes*/ SIZE_OF_INT32_T);
        ############ return the return code
        lw $v0, 4($fp)
        addi $fp, $fp, 8
        #  return return_code_in_register;
        jr $ra
        #}
