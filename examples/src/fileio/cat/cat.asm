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


# C source — see cat.c


# Invocations:
#   spimulator -f cat.asm             # reads stdin
#   spimulator -f cat.asm -           # reads stdin (explicit "-")
#   spimulator -f cat.asm /etc/motd   # reads the file
#   echo hello | spimulator -f cat.asm


#PURPOSE:  cat with real-Unix argv handling.  Block-at-a-time I/O via
#          syscalls 14/15.
#
#          STACK-VISIBLE STYLE: the C locals fd, the argv[1] path (kept
#          for the error message), and the per-chunk byte count n live
#          in stack slots; a scratch register holds only a working
#          COPY.  The read buffer `buf` is `static` in C, so it stays
#          in .data, not on the frame.  This routine makes NO `jal`
#          (only syscalls, which preserve $ra), so it saves $fp but
#          not $ra.  (The old version parked fd/argv1/n in $s*.)
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type     | bytes | role / notes
#  ------------+-------------+------------+------------+-------+---------------------------
#  fd          | -8($fp)     | $t0 (copy) | int        |    4  | STDIN (0) or the opened file descriptor
#  argv1       | -12($fp)    | $t0 (copy) | char *     |    4  | argv[1]; "-"/FILE, kept for error msg
#  n           | -16($fp)    | $v0 (copy) | int        |    4  | bytes read this chunk
#  buf         | buf (.data) | —          | char[4096] | 4096  | static read buffer (C `static` -> .data)
#  (saved $fp) | -4($fp)     | —          | frame ptr  |    4  | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (stdin mode; in read_loop, just read n = 18 bytes into buf,
#            about to write them to stdout)
#
#        main's stack frame             scratch
#   +----------------------------+     +----------------------+
#   | -4($fp)  saved $fp         |     | $t0 = 0 (fd = STDIN)
#   | -8($fp)  fd    = 0 (STDIN) |     | $v0 = 18 (n)
#   | -12($fp) argv1 = 0 (none)  |     +----------------------+
#   | -16($fp) n     = 18        |
#   +----------------------------+
#         $sp = $fp - 16
#
#   Loops read->write until EOF; closes fd (unless STDIN); exits 0.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: fileio/cat/cat.input
#  stdout:
#  | line one
#  | line two

        .data
usageMsg:   .asciiz "usage: cat [FILE|-]\n"
errMsg:     .asciiz "cat: cannot open "
nlMsg:      .asciiz "\n"
buf:        .space 4096

        .text
        .globl main
main:
        # ---- prologue: build a 16-byte frame (no $ra: no `jal`) ----
        addiu $sp, $sp, -16
        sw $fp, 12($sp)              # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 16

        # fd = STDIN;  argv1 = NULL;
        li $t0, 0
        sw $t0, -8($fp)              # fd = 0 (STDIN)
        sw $zero, -12($fp)           # argv1 = 0

        # argc dispatch   (argc arrives in $a0)
        li $t0, 1
        beq $a0, $t0, read_loop      # argc == 1 -> read stdin
        li $t0, 2
        bgt $a0, $t0, usage          # argc > 2 -> usage

        # argc == 2: argv1 = argv[1];   (argv arrives in $a1)
        lw $t0, 4($a1)
        sw $t0, -12($fp)             # argv1 = argv[1]

        # if (argv1[0] == '-' && argv1[1] == 0) read stdin;
        lb $t1, 0($t0)
        bne $t1, '-', do_open
        lb $t1, 1($t0)
        beq $t1, $zero, read_loop

do_open:
        # fd = open(argv1, O_RDONLY, 0);
        lw $a0, -12($fp)             # argv1
        li $v0, 13
        li $a1, 0
        li $a2, 0
        syscall
        bltz $v0, open_failed
        sw $v0, -8($fp)              # fd

read_loop:
        # n = read(fd, buf, 4096);
        li $v0, 14
        lw $a0, -8($fp)              # fd
        la $a1, buf
        li $a2, 4096
        syscall
        blez $v0, close_and_exit
        sw $v0, -16($fp)             # n

        # write(STDOUT, buf, n);
        li $v0, 15
        li $a0, 1
        la $a1, buf
        lw $a2, -16($fp)             # n
        syscall

        j read_loop

close_and_exit:
        lw $t0, -8($fp)              # fd
        beqz $t0, exit_ok            # STDIN -> nothing to close
        li $v0, 16
        lw $a0, -8($fp)              # fd
        syscall
exit_ok:
        # ---- epilogue (success, exit 0) ----------------------------
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
