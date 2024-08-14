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
        #                                                  current SP
        #                                                      |
        #                                                      |
        #                                                      V
        #             ----------------------------------------------------------
        #  C-Variable |   |          |   &c     | &return_code  |           |   |
        #   ----      |---|----------|----------|---------------|-----------|---|
        # RAM Address |   |   --     |  -8($sp) |  -4($sp)      | 0($sp)    |   |
        #   ----      |---|----------|----------|----------     |-----------|---|
        #             |   |          |          |               |           |   |
        #             |   |          |          |               |           |   |
        #   Value     |...|   --     |dontcare  |dontcare       | dontcare  |...|
        #             |   |          |          |               |           |   |
        #   ____      |___|__________|__________|_______________|___________|___|
        #
        #
        #


        move $fp, $sp

        #                                                  current SP
        #                                                 and current FP
        #                                                      |
        #                                                      V
        #             ----------------------------------------------------------
        #  C-Variable |   |          |   &c     | &return_code  |           |   |
        #   ----      |---|----------|----------|---------------|-----------|---|
        # RAM Address |   |   --     |  -8($sp) |  -4($sp)      | 0($sp)    |   |
        #   ----      |---|----------|----------|----------     |-----------|---|
        #             |   |          |          |               |           |   |
        #             |   |          |          |               |           |   |
        #   Value     |...|   --     |dontcare  |dontcare       | dontcare  |...|
        #             |   |          |          |               |           |   |
        #   ____      |___|__________|__________|_______________|___________|___|
        #
        #
        #



        ############ frame pointer = frame_pointer - size of main stack frame
        addi $fp, $fp, -8 # subtract 1 char and 1 int32_t, but align the int to
                          # be at an address mod 4 = 0


        #                                                      current SP
        #                             current FP                   |
        #                                 |                        |
        #                                 V                        V
        #             ----------------------------------------------------------
        #  C-Variable |   |          |   &c     | &return_code  |           |   |
        #   ----      |---|----------|----------|---------------|-----------|---|
        # RAM Address |   |   --     |   0($fp) |  4($fp)       | 0($sp)    |   |
        #   ----      |---|----------|----------|----------     |-----------|---|
        #             |   |          |          |               |           |   |
        #             |   |          |          |               |           |   |
        #   Value     |...|   --     |dontcare  |dontcare       | dontcare  |...|
        #             |   |          |          |               |           |   |
        #   ____      |___|__________|__________|_______________|___________|___|
        #
        #
        #


        ############ set ch
        li $t0, -128
        sw $t0, 0($fp)

        #
        #                                                      current SP
        #                             current FP                   |
        #                                 |                        |
        #                                 V                        V
        #             ----------------------------------------------------------
        #  C-Variable |   |          |    &c    | &return_code  |           |   |
        #   ----      |---|----------|----------|---------------|-----------|---|
        # RAM Address |   |   --     |   0($fp) |  4($fp)       | 0($sp)    |   |
        #   ----      |---|----------|----------|----------     |-----------|---|
        #             |   |          |          |               |           |   |
        #             |   |          |          |               |           |   |
        #   Value     |...|   --     |0xffffff80|dontcare       | dontcare  |...|
        #             |   |          |i.e. -128 |               |           |   |
        #   ____      |___|__________|__________|_______________|___________|___|
        #
        #
        #
        #
        #  How did I get the hex of -128?  well, I'm lazy, and used spimulator to
        #  calculate it for me.
        #       load "05-print-out-ascii/05-print-out-ascii-1.asm"
        #       step
        #       [0x00400004]    0x27a50004  addiu $5, $29, 4 ; 184: addiu $a1 $sp 4              # argv
        #       s
        #       [0x00400008]    0x24a60004  addiu $6, $5, 4 ; 185: addiu $a2 $a1 4              # envp
        #       (spim)
        #       [0x0040000c]    0x00041080  sll $2, $4, 2 ; 186: sll $v0 $a0 2
        #       (spim)
        #       [0x00400010]    0x00c23021  addu $6, $6, $2 ; 187: addu $a2 $a2 $v0
        #       (spim)
        #       [0x00400014]    0x0c100009  jal 0x00400024 [main] ; 188: jal main
        #       (spim)
        #       [0x00400024]    0x001df021  addu $30, $0, $29 ; 53: move $fp, $sp
        #       s
        #       [0x00400028]    0x23de0008  addi $30, $30, 8 ; 75: addi $fp, $fp, 8 # subtract 1 char and 1 int32_t, but align the int to
        #       s
        #       [0x0040002c]    0x3c01ffff  lui $1, -1 ; 98: li $t0, -128
        #       p $t0
        #       Reg 8 = 0x00000000 (0)
        #       s
        #       [0x00400030]    0x3428ff80  ori $8, $1, -128
        #       p $t0
        #       Reg 8 = 0x00000000 (0)
        #       s
        #       [0x00400034]    0xafc80000  sw $8, 0($30) ; 99: sw $t0, 0($fp)
        #       p $t0
        #       Reg 8 = 0xffffff80 (-128)
        #
        #



        ############ set return_value
        li $t0, 0
        sw $t0, 4($fp)

        #                                                      current SP
        #                             current FP                   |
        #                                 |                        |
        #                                 V                        V
        #             ----------------------------------------------------------
        #  C-Variable |   |          |    &c    | &return_code  |           |   |
        #   ----      |---|----------|----------|---------------|-----------|---|
        # RAM Address |   |   --     |   0($fp) |  4($fp)       | 0($sp)    |   |
        #   ----      |---|----------|----------|----------     |-----------|---|
        #             |   |          |          |               |           |   |
        #             |   |          |          |               |           |   |
        #   Value     |...|   --     |0xffffff80|0x00000000     | dontcare  |...|
        #             |   |          |          |               |           |   |
        #   ____      |___|__________|__________|_______________|___________|___|
        #
        #
        #


        # operating_system_print_int(c_in_register);

        lw $a0, 0($fp)
        li $v0, 1
        syscall

        #operating_system_print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall

loopBegin:
        #  c = c + 1;
        lw $t0, 0($fp)
        add $t0, $t0, 1
        sw $t0, 0($fp)

        #                                                      current SP
        #                             current FP                   |
        #                                 |                        |
        #                                 V                        V
        #             -----------------------------------------------------------
        #  C-Variable |   |          |    &c     | &return_code  |           |   |
        #   ----      |---|----------|-----------|---------------|-----------|---|
        # RAM Address |   |   --     |   0($fp)  |  4($fp)       | 0($sp)    |   |
        #   ----      |---|----------|-----------|---------------|-----------|---|
        #             |   |          |           |               |           |   |
        #             |   |          |           |               |           |   |
        #   Value     |...|   --     |incremented|0x00000000     | dontcare  |...|
        #             |   |          | value     |               |           |   |
        #   ____      |___|__________|___________|_______________|___________|___|
        #
        #

        # operating_system_print_int(c_in_register);

        lw $a0, 0($fp)
        li $v0, 1
        syscall

        #operating_system_print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall


        # } while (c != CHAR_MAX);
        lw $t0, 0($fp)
        bne $t0, 127, loopBegin

         ############ return the return code
        lw $v0, 4($fp)
        addi $fp, $fp, 8
        jr $ra
