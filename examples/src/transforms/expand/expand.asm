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


# C source — see expand.c


# Invocations:
#   spimulator -f expand.asm             # reads stdin
#   spimulator -f expand.asm -           # reads stdin (explicit "-")
#   spimulator -f expand.asm Makefile    # reads the file


#PURPOSE:  expand with real-Unix argv handling.  Tab width is hard-coded to 8;
#          the column counter resets to 0 on each '\n'.
#
#          STACK-VISIBLE STYLE: the C locals fd, the argv[1] path, and the output
#          column `col` live in stack slots; a scratch register holds only a
#          working COPY (the per-byte char and the tab-expansion countdown are
#          transients).  This demo makes NO `jal` (only syscalls, which preserve
#          $ra), so it saves $fp but not $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  fd          | -8($fp)     | $t0 (copy) | int       |   4   | STDIN (0) or opened file descriptor
#  argv1       | -12($fp)    | $t0 (copy) | char *    |   4   | argv[1]; "-"/FILE, kept for error msg
#  col         | -16($fp)    | $t0 (copy) | int       |   4   | current output column (resets at '\n')
#  (saved $fp) | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#  No #CANONICAL block: a tab expands to a run of spaces whose exact count a
#  comment anchor would have to reproduce; gated by before/after equivalence.

        .data
usageMsg:   .asciiz "usage: expand [FILE|-]\n"
errMsg:     .asciiz "expand: cannot open "
nlMsg:      .asciiz "\n"
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
        lw $t0, 4($a1)
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
        sw $t0, -16($fp)             # col = 0

read_loop:
        li $v0, 14
        lw $a0, -8($fp)              # fd
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, read_done

        lb $t1, oneByte
        beq $t1, '\t', is_tab
        beq $t1, '\n', is_newline

        # default: print c, col++
        move $a0, $t1
        li $v0, 11
        syscall
        lw $t0, -16($fp)
        addi $t0, $t0, 1
        sw $t0, -16($fp)             # col++
        j read_loop

is_newline:
        li $a0, '\n'
        li $v0, 11
        syscall
        li $t0, 0
        sw $t0, -16($fp)             # col = 0
        j read_loop

is_tab:
        # spaces = 8 - (col & 7)   (spaces countdown is transient in $t2)
        lw $t0, -16($fp)             # col
        andi $t2, $t0, 7
        li $a0, 8
        sub $t2, $a0, $t2
tab_loop:
        blez $t2, read_loop
        li $a0, ' '
        li $v0, 11
        syscall
        lw $t0, -16($fp)
        addi $t0, $t0, 1
        sw $t0, -16($fp)             # col++
        addi $t2, $t2, -1
        j tab_loop

read_done:
        lw $t0, -8($fp)              # fd
        beqz $t0, exit_ok
        li $v0, 16
        lw $a0, -8($fp)
        syscall
exit_ok:
        li $v0, 0
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
