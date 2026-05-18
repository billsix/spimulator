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


# C source — see 14-rot13-1.c
#
#     int ch;
#     while ((ch = read_char()) != -1) {
#       if (ch >= 'a' && ch <= 'z') ch = 'a' + (ch - 'a' + 13) % 26;
#       else if (ch >= 'A' && ch <= 'Z') ch = 'A' + (ch - 'A' + 13) % 26;
#       print_char((char)ch);
#     }
#
# Invocation:
#   echo "Hello, World!~" | spimulator -f 14-rot13.asm
#   # => Uryyb, Jbeyq!


#PURPOSE:  ROT13: rotate each alphabetic byte 13 places forward
#          in its case-respective alphabet, with wraparound.
#          Non-alphabetic bytes pass through unchanged.  Same
#          byte-stream shape as 13-tr; the new idea is the
#          modulo-26 step that handles the wraparound.
#
#NOTES:    The sentinel is `~` (matching 13-tr / 15-expand) since
#          spim's read_char doesn't signal EOF and we need a way
#          to tell the program "stop reading."  `~` is not in
#          either case range, so it never gets transformed.
#
#          ROT13 is self-inverse.  Piping the same text through
#          the program twice gets the original back -- a free
#          end-to-end sanity check.
#
#          The modulo: `(offset + 13) % 26` becomes
#              addi $t, $t, 13
#              li   $tx, 26
#              div  $t, $tx
#              mfhi $t            ; remainder lands in HI
#          The quotient goes in LO and we ignore it.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     ch            $t0                  (current byte being processed)
#     offset        $t1                  ((ch - base) + 13 before the
#                                          div; the remainder after the
#                                          mfhi)
#     26            $t2                  (modulus, loaded just before
#                                          each div)
#
#   No subroutine calls; no Cross-call saves section.  Three
#   $t-regs reused across the read_char/print_char syscalls per
#   the spim-preserves-$t convention (same shape as
#   06/12/13/16/17).
#
#   Volatile:
#     $a0   syscall arg
#     $v0   syscall selector / read_char return
#           (12 = read_char, 11 = print_char)

        .text
        .globl main
main:
loop:
        # ch = read_char()
        li $v0, 12
        syscall
        move $t0, $v0

        beq $t0, '~', done           # sentinel exit

        # if (ch >= 'a' && ch <= 'z')
        blt $t0, 'a', try_upper
        bgt $t0, 'z', try_upper

        # ch = 'a' + (ch - 'a' + 13) % 26
        # (spim's assembler doesn't accept -'a', so we use -97 explicitly)
        addi $t1, $t0, -97           # offset = ch - 'a'   ('a' = 97)
        addi $t1, $t1, 13            # offset + 13
        li $t2, 26
        div $t1, $t2                 # lo = q, hi = r
        mfhi $t1                     # offset = (offset + 13) % 26
        addi $t0, $t1, 'a'           # ch = 'a' + offset
        j emit

try_upper:
        # if (ch >= 'A' && ch <= 'Z')
        blt $t0, 'A', emit
        bgt $t0, 'Z', emit

        # ch = 'A' + (ch - 'A' + 13) % 26
        addi $t1, $t0, -65           # offset = ch - 'A'   ('A' = 65)
        addi $t1, $t1, 13
        li $t2, 26
        div $t1, $t2
        mfhi $t1
        addi $t0, $t1, 'A'

emit:
        # print_char(ch)
        move $a0, $t0
        li $v0, 11
        syscall
        j loop

done:
        li $v0, 0
        jr $ra
