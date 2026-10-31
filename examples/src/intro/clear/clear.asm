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


# C source — see clear.c
# Same shape as helloworld: write a fixed string, exit zero.
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
#          `ubase/clear` in a handful of instructions.
#
#          STACK-VISIBLE STYLE: this program has NO C local variables
#          — just a fixed string in .data — and makes NO `jal`, so
#          there is no stack frame (a frame with nothing in it would
#          be ceremony).  The string lives in .data; its ADDRESS is
#          what travels in a register ($a0).  Contrast
#          print1through10, whose loop counter does get a stack slot.
#          There is no #CANONICAL block: the output is terminal
#          control bytes (ESC ...), not text the checker can diff.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable      | memory addr         | register   | C type  | bytes | role / notes
#  ----------------+---------------------+------------+---------+-------+---------------------------
#  "\033[2J\033[H" | clearString (.data) | $a0 (addr) | char[8] |   8   | escape sequence (7 bytes + NUL); address passed to print_string
#
# ------------------------------------------------------------------
#  SNAPSHOT (no args; just before the syscall)
#
#        .data                             scratch
#   +-----------------------------+       +-------------------------------+
#   | clearString: 1b 5b 32 4a    |       | $a0 = &clearString (its addr)
#   |              1b 5b 48 00     | <---- | $v0 = 4 (print_string)
#   +-----------------------------+       +-------------------------------+
#
#   Writes the 7 control bytes, then exits 0.  There is no frame and
#   no C variable to trace — the bytes live in .data the whole time.

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
