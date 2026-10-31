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


# C source — see head.c


# Invocations:
#   spimulator -f head.asm                       # stdin, first 10 lines
#   spimulator -f head.asm -n 3                  # stdin, first 3 lines
#   spimulator -f head.asm -n 5 /etc/services    # file, first 5 lines
#   seq 1 100 | spimulator -f head.asm -n 4      # pipe in, first 4 lines


#PURPOSE:  head with real-Unix argv handling: the full `head [-n N] [FILE|-]`
#          shape.
#
#          STACK-VISIBLE STYLE: all of main's C locals — fd, the line limit n,
#          the file_arg pointer, the argv vector, and the running line_count —
#          live in stack slots; a scratch register holds only a working COPY.
#          main makes jals (str_eq, atoi), so it saves $ra.  str_eq and atoi are
#          leaf helpers, kept register-based.  The one-byte read pad stays .data.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  fd          | -12($fp)    | $t0 (copy) | int       |   4   | STDIN (0) or opened file descriptor
#  n           | -16($fp)    | $t1 (copy) | int       |   4   | line limit (default 10, or -n N)
#  file_arg    | -20($fp)    | $t0 (copy) | char *    |   4   | NULL, "-", or FILE from argv
#  argv        | -24($fp)    | $t0 (copy) | char **   |   4   | entry $a1, kept across the jals
#  line_count  | -28($fp)    | $t0 (copy) | int       |   4   | newlines echoed so far (stop at n)
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | jal str_eq / jal atoi
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: transforms/head/head.input
#  stdout:
#  | line01
#  | line02
#  | line03
#  | line04
#  | line05
#  | line06
#  | line07
#  | line08
#  | line09
#  | line10

        .data
usageMsg:   .asciiz "usage: head [-n N] [FILE|-]\n"
errMsg:     .asciiz "head: cannot open "
nlMsg:      .asciiz "\n"
flagN:      .asciiz "-n"
oneByte:    .space 1

        .text
        .globl main
main:
        addiu $sp, $sp, -32
        sw $ra, 28($sp)              # -> -4($fp)
        sw $fp, 24($sp)              # -> -8($fp)
        addiu $fp, $sp, 32

        li $t0, 0
        sw $t0, -12($fp)             # fd = STDIN (0)
        li $t0, 10
        sw $t0, -16($fp)             # n = 10
        sw $zero, -20($fp)           # file_arg = NULL
        sw $a1, -24($fp)             # argv

        # Dispatch on argc ($a0).
        li $t0, 1
        beq $a0, $t0, open_if_needed   # argc==1 -> stdin, n=10
        li $t0, 2
        beq $a0, $t0, argc_two
        li $t0, 3
        beq $a0, $t0, argc_three
        li $t0, 4
        beq $a0, $t0, argc_four
        j usage

argc_two:
        # head FILE_OR_DASH
        lw $t0, -24($fp)             # argv
        lw $t0, 4($t0)               # argv[1]
        sw $t0, -20($fp)             # file_arg
        j open_if_needed

argc_three:
        # head -n N
        lw $t0, -24($fp)
        lw $a0, 4($t0)               # argv[1]
        la $a1, flagN
        jal str_eq
        beqz $v0, usage
        lw $t0, -24($fp)
        lw $a0, 8($t0)               # argv[2]
        jal atoi
        sw $v0, -16($fp)             # n
        j open_if_needed

argc_four:
        # head -n N FILE_OR_DASH
        lw $t0, -24($fp)
        lw $a0, 4($t0)
        la $a1, flagN
        jal str_eq
        beqz $v0, usage
        lw $t0, -24($fp)
        lw $a0, 8($t0)
        jal atoi
        sw $v0, -16($fp)             # n
        lw $t0, -24($fp)
        lw $t0, 12($t0)              # argv[3]
        sw $t0, -20($fp)             # file_arg
        # fall through

open_if_needed:
        lw $t0, -20($fp)             # file_arg
        beqz $t0, read_setup         # no file_arg -> stdin
        lb $t1, 0($t0)
        bne $t1, '-', do_open
        lb $t1, 1($t0)
        beq $t1, $0, read_setup      # file_arg is "-" -> stdin

do_open:
        lw $a0, -20($fp)             # file_arg
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        sw $v0, -12($fp)             # fd

read_setup:
        li $t0, 0
        sw $t0, -28($fp)             # line_count = 0

read_loop:
        # while (line_count < n)
        lw $t0, -28($fp)             # line_count
        lw $t1, -16($fp)             # n
        bge $t0, $t1, read_done
        # read(fd, &oneByte, 1)
        li $v0, 14
        lw $a0, -12($fp)             # fd
        la $a1, oneByte
        li $a2, 1
        syscall
        blez $v0, read_done

        # print_char(c)
        lb $a0, oneByte
        li $v0, 11
        syscall
        bne $a0, '\n', read_loop
        lw $t0, -28($fp)
        addi $t0, $t0, 1
        sw $t0, -28($fp)             # line_count++
        j read_loop

read_done:
        lw $t0, -12($fp)             # fd
        beqz $t0, exit_ok
        li $v0, 16
        lw $a0, -12($fp)
        syscall
exit_ok:
        li $v0, 0
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 32
        jr $ra

open_failed:
        li $v0, 4
        la $a0, errMsg
        syscall
        li $v0, 4
        lw $a0, -20($fp)             # file_arg
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


# ---------- str_eq (leaf helper) — $v0 = 1 if equal, 0 if not --------
# Input: $a0, $a1 = pointers
str_eq:
        lb $t0, ($a0)
        lb $t1, ($a1)
        bne $t0, $t1, str_eq_no
        beq $t0, $0, str_eq_yes
        addi $a0, $a0, 1
        addi $a1, $a1, 1
        j str_eq
str_eq_yes:
        li $v0, 1
        jr $ra
str_eq_no:
        li $v0, 0
        jr $ra


# ---------- atoi (leaf helper) — parse decimal int from $a0 ----------
atoi:
        li $v0, 0
        li $t1, 1
        lb $t0, ($a0)
        bne $t0, '-', atoi_loop
        li $t1, -1
        addi $a0, $a0, 1
atoi_loop:
        lb $t0, ($a0)
        blt $t0, '0', atoi_done
        bgt $t0, '9', atoi_done
        addi $t0, $t0, -48
        li $t2, 10
        mult $v0, $t2
        mflo $v0
        add $v0, $v0, $t0
        addi $a0, $a0, 1
        j atoi_loop
atoi_done:
        mult $v0, $t1
        mflo $v0
        jr $ra
