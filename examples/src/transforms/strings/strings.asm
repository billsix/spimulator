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


#PURPOSE:  Simplified `strings`: print every run of 4+ printable ASCII bytes
#          (0x20..0x7e) from stdin, one per line.  Control/high-bit bytes and EOF
#          terminate a run.  Only the first 4 bytes of a candidate run are held
#          back (in `pending`); once a run reaches 4 they are flushed and later
#          bytes stream straight through — a two-phase state machine.
#
#INVOCATION (binary-ish input from a file or pipe):
#    spimulator -f strings.asm < /some/binary
#    printf 'Hi\0Hello\1World!' | spimulator -f strings.asm
#
#          STACK-VISIBLE STYLE: the C locals `run` (current printable-run length)
#          and `ch` (the byte just read) live in stack slots; a scratch register
#          holds only a working COPY (the flush index is a transient).  The 4-byte
#          `pending` hold-back buffer is file-scope, so it stays in .data.  This
#          demo makes NO `jal` (only syscalls, which preserve $ra), so it saves
#          $fp but not $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr     | register   | C type    | bytes | role / notes
#  ------------+-----------------+------------+-----------+-------+---------------------------
#  run         | -8($fp)         | $t1 (copy) | int       |   4   | current printable-run length
#  ch          | -12($fp)        | $t0 (copy) | int       |   4   | byte from read_char (-1 at EOF)
#  pending     | pending (.data) | —          | char[4]   |   4   | first 4 bytes of a candidate run
#  (saved $fp) | -4($fp)         | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: transforms/strings/strings.input
#  stdout:
#  | Hello
#  | World!
#  | Assembly rocks
#  | tail

        .data
pending:    .space 4                     # first MINRUN bytes of a run

        .text
        .globl main
main:
        addiu $sp, $sp, -16
        sw $fp, 12($sp)              # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 16

        li $t0, 0
        sw $t0, -8($fp)              # run = 0

loop:
        # ch = read_char();
        li $v0, 12
        syscall
        sw $v0, -12($fp)             # ch = result
        lw $t0, -12($fp)
        bltz $t0, eof                # -1 -> EOF

        # printable?  ' ' (0x20) <= ch <= '~' (0x7e)
        blt $t0, ' ', nonprintable
        bgt $t0, '~', nonprintable

        # printable byte: if (run < 4) pending[run] = ch;
        lw $t1, -8($fp)              # run
        bge $t1, 4, past_pending
        la $t2, pending
        add $t2, $t2, $t1
        lw $t0, -12($fp)             # ch
        sb $t0, ($t2)
past_pending:
        # run++;
        lw $t1, -8($fp)
        addi $t1, $t1, 1
        sw $t1, -8($fp)
        # if (run == 4) flush; else if (run > 4) stream ch; else keep buffering
        li $t4, 4
        beq $t1, $t4, flush_pending
        ble $t1, $t4, loop           # run < 4: still buffering
        lw $a0, -12($fp)             # run > 4: stream ch straight through
        li $v0, 11
        syscall
        j loop

flush_pending:
        # for (i = 0; i < 4; i++) print_char(pending[i]);   (i transient in $t3)
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
        lw $t1, -8($fp)              # run
        blt $t1, 4, reset_run
        li $a0, '\n'
        li $v0, 11
        syscall
reset_run:
        li $t0, 0
        sw $t0, -8($fp)              # run = 0
        j loop

eof:
        # final run may end at EOF rather than at a non-printable byte
        lw $t1, -8($fp)              # run
        blt $t1, 4, done
        li $a0, '\n'
        li $v0, 11
        syscall
done:
        li $v0, 0                    # exit status: __start passes this through
        lw $fp, -4($fp)
        addiu $sp, $sp, 16
        jr $ra
