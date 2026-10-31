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


# C source — see rot13-1.c
# Invocation:
#   echo "Hello, World!~" | spimulator -f rot13.asm   # => Uryyb, Jbeyq!~


#PURPOSE:  ROT13: rotate each alphabetic byte 13 places forward in its
#          case-respective alphabet, with wraparound (modulo 26).
#          Non-alphabetic bytes pass through unchanged.
#
#NOTES:    `read_char` returns -1 at EOF; the loop branches on `bltz`.
#          ROT13 is self-inverse.  The modulo `(offset + 13) % 26` is
#          `div`/`mfhi` (remainder in HI; the LO quotient is ignored).
#
#          STACK-VISIBLE STYLE: the C local `ch` (current byte) lives in a stack
#          slot; the transformed byte is stored back before it is printed.  The
#          `offset`/modulus are transient sub-expression temporaries kept in
#          registers (produced and consumed within one iteration).  This demo
#          makes NO `jal` (only syscalls, which preserve $ra), so it saves $fp
#          but not $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  ch          | -8($fp)     | $t0 (copy) | int       |   4   | current byte (-1 at EOF); rotated in place
#  offset      | —           | $t1        | int       |   4   | transient (ch-base+13), then the % 26 result
#  (saved $fp) | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: transforms/rot13/rot13.input
#  stdout:
#  | Uryyb, Jbeyq!~

        .text
        .globl main
main:
        addiu $sp, $sp, -8
        sw $fp, 4($sp)               # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 8

loop:
        # ch = read_char()
        li $v0, 12
        syscall
        sw $v0, -8($fp)              # ch = result
        lw $t0, -8($fp)
        bltz $t0, done               # -1 -> EOF

        # if (ch >= 'a' && ch <= 'z')
        blt $t0, 'a', try_upper
        bgt $t0, 'z', try_upper
        # ch = 'a' + (ch - 'a' + 13) % 26  ('a' = 97; spim rejects -'a')
        addi $t1, $t0, -97           # offset = ch - 'a'
        addi $t1, $t1, 13            # offset + 13
        li $t2, 26
        div $t1, $t2                 # lo = q, hi = r
        mfhi $t1                     # offset = (offset + 13) % 26
        addi $t0, $t1, 'a'           # ch = 'a' + offset
        sw $t0, -8($fp)              # store the rotated byte back
        j emit

try_upper:
        # if (ch >= 'A' && ch <= 'Z')   ($t0 still holds ch)
        blt $t0, 'A', emit
        bgt $t0, 'Z', emit
        # ch = 'A' + (ch - 'A' + 13) % 26  ('A' = 65)
        addi $t1, $t0, -65
        addi $t1, $t1, 13
        li $t2, 26
        div $t1, $t2
        mfhi $t1
        addi $t0, $t1, 'A'
        sw $t0, -8($fp)              # store the rotated byte back

emit:
        # print_char(ch)
        lw $a0, -8($fp)
        li $v0, 11
        syscall
        j loop

done:
        li $v0, 0
        lw $fp, -4($fp)
        addiu $sp, $sp, 8
        jr $ra
