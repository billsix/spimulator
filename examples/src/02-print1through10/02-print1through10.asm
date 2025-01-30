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
        # make the frame pointer be the stack pointer
        move $fp, $sp
        # frame pointer = frame_pointer - size of main stack frame
        addi $fp, $fp, -8 # subtract 3 int32_t and one pointer, each of which are 4 bytes

        #  {
        #    int32_t i_in_register = 0;
        li $t0, 0       # stackframe i has to go into a register before
                        # being put on the stack
        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
        #            /*src*/ &i_in_register,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        sw $t0, 0($fp)  # now stackframe i can go on the stack,
                        # at 0 offset from stack pointer
                        # our use of $t1 for set is is now complete
        #    int return_value_in_register = EXIT_SUCCESS;
        li $t0, 0       # stackframe return has to go into a register before
                        # being put on the stack
        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
        #            /*src*/ &return_value_in_register,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        sw $t0, 4($fp)  # now stackframe returncode can go on the stack,
                        # at 4 offset from stack pointer
                        # our use of $t1 for set is is now complete
        #  }
        #
        #beginningOfLoop : {
beginningOfLoop:
        #  int32_t i_in_register;
        #  xmemcpy(/*dest*/ &i_in_register,
        #          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
        #          SIZE_OF_INT32_T);
        lw $t0, 0($fp)  # now stackframe i goes into register $t1
        #  if (!(i_in_register <= 10))
        #    goto endOfLoop;
        #}
        bgt $t0, 10, endOfLoop # negate the test because, by default,
                               # if the test fails, the loopBody instruction
                               #
        #loopBody : {
loopBody:
        #  int32_t i_in_register;
        #  xmemcpy(/*dest*/ &i_in_register,
        #          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
        #          SIZE_OF_INT32_T);
        lw $t0, 0($fp)   # now stackframe i goes into register $t1
        #  operating_system_print_int(i_in_register);
        #}

        # print integer. $v0 must be one, int to print in $a0, syscall
        # stack frame i is in $t0, copy it to $a0
        move $a0, $t0
        li $v0, 1
        syscall

        #  operating_system_print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall

        #  {
        #    int32_t i_in_register;
        #    xmemcpy(/*dest*/ &i_in_register,
        #            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
        #            SIZE_OF_INT32_T);
        lw $t0, 0($fp)
        #    int32_t incrementedI_in_register = i_in_register + 1;
        addi $t0, $t0, 1
        #    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_I,
        #            /*src*/ &incrementedI_in_register,
        #            /*numberOfBytes*/ SIZE_OF_INT32_T);
        #  }
        sw $t0, 0($fp)

        #  goto beginningOfLoop;
        b beginningOfLoop

        #endOfLoop : {
endOfLoop:
        #  int32_t return_value_in_register;
        #  xmemcpy(/*dest*/ &return_value_in_register,
        #          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
        #          SIZE_OF_INT32_T);
        lw $v0, 4($fp)
        #  return return_value_in_register;
        #}
        addi $fp, $fp, 8
        jr $ra
