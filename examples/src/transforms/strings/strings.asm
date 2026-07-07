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


# C source — see strings.c


#PURPOSE:  Simplified `strings`: print every run of 4+ printable
#          ASCII bytes (0x20..0x7e) from stdin, one per line.
#          Control bytes, high-bit bytes, and EOF terminate a run.
#
#          No line buffer, no length limit: only the first 4 bytes of
#          a candidate run are held back (in `pending`).  Once a run
#          reaches 4 the held bytes are flushed and every later byte
#          streams straight through — a two-phase state machine.
#
#INVOCATION (binary-ish input comes from a file or pipe):
#
#    spimulator -f strings.asm < /some/binary
#    printf 'Hi\0Hello\1World!' | spimulator -f strings.asm
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     run           $s1                  (current printable-run length)
#     pending       `pending` (.data)    (first 4 bytes of a candidate run)
#     ch            $t0                  (byte from read_char; -1 = EOF)
#     i             $t3                  (flush-loop index)
#
#   Volatile:
#     $a0   syscall arg (byte to print)
#     $v0   syscall selector / read_char result

        .data
pending:    .space 4                     # first MINRUN bytes of a run

        .text
        .globl main
main:
        li $s1, 0                    # run = 0

loop:
        # ch = read_char();
        li $v0, 12
        syscall
        move $t0, $v0

        bltz $t0, eof                # -1 -> EOF

        # printable?  ' ' (0x20) <= ch <= '~' (0x7e)
        blt $t0, ' ', nonprintable
        bgt $t0, '~', nonprintable

        # printable byte:
        #   if (run < 4) pending[run] = ch;
        bge $s1, 4, past_pending
        la $t2, pending
        add $t2, $t2, $s1
        sb $t0, ($t2)
past_pending:
        #   run++;
        addi $s1, $s1, 1

        #   if (run == 4) flush the 4 pending bytes
        li $t1, 4
        beq $s1, $t1, flush_pending
        #   else if (run > 4) print ch directly
        ble $s1, $t1, loop           # run < 4: still buffering
        move $a0, $t0                # run > 4: stream straight through
        li $v0, 11
        syscall
        j loop

flush_pending:
        # for (i = 0; i < 4; i++) print_char(pending[i]);
        li $t3, 0
flush_loop:
        la $t2, pending
        add $t2, $t2, $t3
        lbu $a0, ($t2)
        li $v0, 11
        syscall
        addi $t3, $t3, 1
        blt $t3, 4, flush_loop
        j loop

nonprintable:
        # if (run >= 4) print_char('\n');  run = 0;
        blt $s1, 4, reset_run
        li $a0, '\n'
        li $v0, 11
        syscall
reset_run:
        li $s1, 0
        j loop

eof:
        # final run may end at EOF rather than at a non-printable byte
        blt $s1, 4, done
        li $a0, '\n'
        li $v0, 11
        syscall
done:
        li $v0, 0                    # exit status: __start passes this through
        jr $ra
