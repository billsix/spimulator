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


# C source — see rev.c


# Invocations:
#   spimulator -f rev.asm             # reads stdin
#   spimulator -f rev.asm -           # reads stdin (explicit "-")
#   spimulator -f rev.asm /etc/motd   # reads the file


#PURPOSE:  rev with real-Unix argv handling.  Buffers each line in `buf`
#          (256 bytes), then walks it backwards on newline.
#
#          STACK-VISIBLE STYLE: the C locals fd, the argv[1] path (kept for the
#          error message), and the current line length len live in stack slots;
#          a scratch register holds only a working COPY (the backward-walk index
#          is a transient).  The line buffer `buf` and the one-byte read pad are
#          file-scope data, so they stay in .data.  This demo makes NO `jal`
#          (only syscalls, which preserve $ra), so it saves $fp but not $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr     | register   | C type    | bytes | role / notes
#  ------------+-----------------+------------+-----------+-------+---------------------------
#  fd          | -8($fp)         | $t0 (copy) | int       |   4   | STDIN (0) or the opened file descriptor
#  argv1       | -12($fp)        | $t0 (copy) | char *    |   4   | argv[1]; "-"/FILE, kept for error msg
#  len         | -16($fp)        | $t2 (copy) | int       |   4   | current line length (0..256)
#  buf         | buf (.data)     | —          | char[256] |  256  | static line buffer
#  (saved $fp) | -4($fp)         | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: transforms/rev/rev.input
#  stdout:
#  | cba
#  | zyx

        .data
usageMsg:   .asciiz "usage: rev [FILE|-]\n"
errMsg:     .asciiz "rev: cannot open "
nlMsg:      .asciiz "\n"
buf:        .space 256
oneByte:    .space 1

        .text
        .globl main
main:
        addiu $sp, $sp, -16
        sw $fp, 12($sp)              # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 16

        li $t0, 0
        sw $t0, -8($fp)              # fd = STDIN (0)
        sw $zero, -12($fp)           # argv1 = 0

        li $t0, 1
        beq $a0, $t0, read_setup
        li $t0, 2
        bgt $a0, $t0, usage

        lw $t0, 4($a1)               # argv[1]
        sw $t0, -12($fp)             # argv1
        lb $t1, 0($t0)
        bne $t1, '-', do_open
        lb $t1, 1($t0)
        beq $t1, $0, read_setup

do_open:
        lw $a0, -12($fp)
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        sw $v0, -8($fp)              # fd

read_setup:
        li $t0, 0
        sw $t0, -16($fp)             # len = 0

read_loop:
        li $v0, 14
        lw $a0, -8($fp)              # fd
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, read_done          # EOF

        lb $t0, oneByte
        beq $t0, '\n', flush_line

        # else if (len < 256) buf[len++] = c
        li $t1, 256
        lw $t2, -16($fp)             # len
        bge $t2, $t1, read_loop      # buffer full -> drop byte
        la $t1, buf
        add $t1, $t1, $t2            # &buf[len]
        sb $t0, ($t1)
        addi $t2, $t2, 1
        sw $t2, -16($fp)             # len++
        j read_loop

flush_line:
        # walk buf backwards
        lw $t2, -16($fp)             # len
        addi $t3, $t2, -1            # i = len - 1 (transient)
back_loop:
        bltz $t3, after_back
        la $t1, buf
        add $t1, $t1, $t3
        lb $a0, ($t1)
        li $v0, 11
        syscall
        addi $t3, $t3, -1
        j back_loop
after_back:
        li $a0, '\n'
        li $v0, 11
        syscall
        li $t0, 0
        sw $t0, -16($fp)             # len = 0
        j read_loop

read_done:
        # flush trailing partial line (no newline at end of input)
        lw $t2, -16($fp)             # len
        blez $t2, after_tail
        addi $t3, $t2, -1
tail_loop:
        bltz $t3, after_tail
        la $t1, buf
        add $t1, $t1, $t3
        lb $a0, ($t1)
        li $v0, 11
        syscall
        addi $t3, $t3, -1
        j tail_loop
after_tail:
        # close fd if not STDIN
        lw $t0, -8($fp)              # fd
        beqz $t0, exit_ok
        li $v0, 16
        lw $a0, -8($fp)
        syscall
exit_ok:
        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $fp, -4($fp)
        addiu $sp, $sp, 16
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        lw $a0, -12($fp)             # argv1
        li $v0, 4
        syscall
        li $v0, 4
        la $a0, nlMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall
