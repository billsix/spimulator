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


# C source — see tr.c


# Invocations (pure stdin filter — like real `tr`, no file arg):
#   echo hello | spimulator -f tr.asm           # => HELLO
#   spimulator -f tr.asm < /etc/motd            # upcase a file via redirect


#PURPOSE:  Copy stdin to stdout, upcasing every lowercase ASCII byte.
#          Byte-level conditional transformation: read a byte, test a range,
#          optionally mutate it, write it.
#
#NOTES:    `read_char` returns -1 at EOF; the loop branches on `bltz`.
#
#          STACK-VISIBLE STYLE: the C local `ch` (current byte) lives in a stack
#          slot; a scratch register holds a working COPY, and the upcased byte is
#          stored back before it is printed.  This demo makes NO `jal` (only
#          syscalls, which preserve $ra), so it saves $fp but not $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  ch          | -8($fp)     | $t0 (copy) | int       |   4   | current byte (-1 at EOF); upcased in place
#  (saved $fp) | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: transforms/tr/tr.input
#  stdout:
#  | HELLO

        .text
        .globl main
main:
        addiu $sp, $sp, -8
        sw $fp, 4($sp)               # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 8

loop:
        # ch = read_char();
        li $v0, 12
        syscall
        sw $v0, -8($fp)              # ch = result
        lw $t0, -8($fp)
        bltz $t0, done               # -1 -> EOF

        # if (ch < 'a' || ch > 'z') pass through; else ch -= 32 (upcase)
        blt $t0, 'a', write
        bgt $t0, 'z', write
        addi $t0, $t0, -32
        sw $t0, -8($fp)              # store the upcased byte back

write:
        # print_char(ch);
        lw $a0, -8($fp)
        li $v0, 11
        syscall
        j loop

done:
        li $v0, 0
        lw $fp, -4($fp)
        addiu $sp, $sp, 8
        jr $ra
