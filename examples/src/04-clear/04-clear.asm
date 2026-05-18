# Copyright (c) 2021-2026 William Emerison Six
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


# C source — see 04-clear.c
#
#     __attribute__((noreturn)) void _start(void) {
#       print_string("\033[2J\033[H");
#       os_exit(0);
#     }
#
# Same shape as 01-helloworld: write a fixed string, exit zero.
# The interesting thing is what the bytes ARE — control codes the
# terminal interprets rather than displayable characters.
#
#   \033   = 0x1b = ESC
#   [2J    = "erase entire screen"
#   \033[H = "move the cursor to row 1, column 1"
#
# Octal `\033` rather than hex `\x1b` because spim's `.asciiz`
# recognises octal escapes and `\X` (uppercase), but NOT the
# lowercase `\x` form most C code uses.


#PURPOSE:  Clear the terminal and home the cursor.  Suckless
#          `ubase/clear` in 6 instructions.
#
#VARIABLES:
#   $a0       syscall argument register
#   $v0       syscall selector; also main's return value
#               (syscall 4 = print_string)

        .data
clearString:
        .asciiz     "\033[2J\033[H"

        .text
        .globl main
main:
        li $v0, 4                    # syscall 4 = print_string
        la $a0, clearString          # arg = address of the escape sequence
        syscall                      # ask the OS to print it

        li $v0, 0                    # exit status 0
        jr $ra                       # return to the runtime
