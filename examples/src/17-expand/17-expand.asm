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


# C source — see 17-expand-1.c
#
#     __attribute__((noreturn)) void _start(void) {
#       int col = 0;
#       int ch = read_char();
#       while (ch != -1) {
#         if (ch == '\t') {
#           int spaces = 8 - (col % 8);
#           for (int i = 0; i < spaces; i++) print_char(' ');
#           col += spaces;
#         } else if (ch == '\n') {
#           print_char('\n');
#           col = 0;
#         } else {
#           print_char(ch);
#           col += 1;
#         }
#         ch = read_char();
#       }
#       os_exit(0);
#     }


#PURPOSE:  Replace each tab on stdin with enough spaces to reach
#          the next 8-column boundary.  Maintains a `col` counter
#          across the input stream — the loop emits a variable
#          number of output bytes for each input byte (one for
#          ordinary chars, 1-8 for tabs, 1 for newline + reset).
#
#NOTES:    SPIM's read_char has no EOF; this .asm uses '~' as the
#          terminating sentinel.  '~' is rarely a meaningful byte
#          in human-readable text, and it isn't a tab or newline
#          so it doesn't collide with the special cases.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     col           $t0                  (0-based output column;
#                                          incremented by every emitted
#                                          byte, reset to 0 by '\n')
#     ch            $t1                  (most recently read byte)
#     spaces        $t2                  (tab-expansion countdown 1..8,
#                                          initialised as 8 - (col & 7))
#
#   No subroutine calls; no Cross-call saves section.  Three $t-
#   regs reused across every syscall per the same spim-preserves-
#   $t convention as 06/12/13.
#
#   Volatile (no preserved meaning across a syscall, by convention):
#     $a0   syscall arg
#     $v0   syscall selector / read_char return
#           (12 = read_char, 11 = print_char)

        .text
        .globl main
main:
        li $t0, 0                    # col = 0

loop:
        # ch = read_char();
        li $v0, 12
        syscall
        move $t1, $v0

        beq $t1, '~', done           # sentinel exit
        beq $t1, '\t', is_tab
        beq $t1, '\n', is_newline

        # default branch: print the char, col++
        move $a0, $t1
        li $v0, 11
        syscall
        addi $t0, $t0, 1
        j loop

is_newline:
        li $a0, '\n'
        li $v0, 11
        syscall
        li $t0, 0                    # col = 0
        j loop

is_tab:
        # spaces = 8 - (col & 7)        -- bitwise mod-8
        andi $t2, $t0, 7             # $t2 = col & 7
        li $a0, 8
        sub $t2, $a0, $t2            # $t2 = 8 - (col & 7)
tab_loop:
        blez $t2, loop               # done emitting spaces -> resume
        li $a0, ' '
        li $v0, 11
        syscall
        addi $t0, $t0, 1             # col++
        addi $t2, $t2, -1            # spaces--
        j tab_loop

done:
        li $v0, 0
        jr $ra
