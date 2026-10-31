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


# C source — see nologin.c


#PURPOSE:  Print /etc/nologin.txt if it exists, else a fallback message.
#          Always exit 1.  First demo using open (13) and close (16).
#
#NOTES:    Returning from `main` via `jr $ra` ALWAYS makes spim exit 0
#          (the runtime's __start does `li $v0,10; syscall`, which
#          stores status 0).  To exit non-zero we issue syscall 17
#          (exit2) ourselves with the status in $a0 — BOTH paths here
#          do that, so this routine never returns and never tears its
#          frame down (process exit reclaims the stack).
#
#          STACK-VISIBLE STYLE: the C locals fd and n live in stack
#          slots; a scratch register holds only a working COPY.  The
#          read buffer `buf` is `static` in C, so it stays in .data,
#          not on the frame.  No `jal` (only syscalls), so $fp is set
#          up for -N($fp) addressing but $ra is never at risk.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#          No #CANONICAL block: the output depends on whether
#          /etc/nologin.txt exists on the host (the fallback string is
#          printed otherwise), so it is not a deterministic anchor.
#          Gated by before/after output equivalence in a fixed env.
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr  | register   | C type     | bytes | role / notes
#  ------------+--------------+------------+------------+-------+---------------------------
#  fd          | -8($fp)      | $v0 (copy) | int        |    4  | open() result; used by read and close
#  n           | -12($fp)     | $v0 (copy) | int        |    4  | bytes read; passed to write
#  buf         | buf (.data)  | —          | char[256]  |  256  | static read buffer (C `static` -> .data)
#  path        | path (.data) | —          | char[]     |       | "/etc/nologin.txt"
#  (saved $fp) | -4($fp)      | —          | frame ptr  |    4  | caller's $fp (never restored: always exits via syscall 17)
#
# ------------------------------------------------------------------
#  SNAPSHOT (no args; in read_loop, having just read n bytes of the
#            file into buf, about to write them to stdout)
#
#        main's stack frame             .data
#   +----------------------------+     +---------------------------+
#   | -4($fp)  saved $fp         |     | buf: <file bytes...>      |
#   | -8($fp)  fd  = 3           |     | path: "/etc/nologin.txt"  |
#   | -12($fp) n   = 42          |     +---------------------------+
#   +----------------------------+
#         $sp = $fp - 12
#
#   Loops read->write until EOF, closes fd, exits 1.  If the open
#   fails it prints the fallback string and exits 1.

        .data
path:           .asciiz     "/etc/nologin.txt"
altMessage:     .asciiz     "The account is currently unavailable.\n"
buf:            .space 256

        .text
        .globl main
main:
        # ---- prologue: build a 12-byte frame (no $ra: no `jal`) ----
        addiu $sp, $sp, -12
        sw $fp, 8($sp)               # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 12

        # fd = open(path, O_RDONLY, 0);
        li $v0, 13                   # syscall 13 = open
        la $a0, path
        li $a1, 0                    # O_RDONLY (= 0 on Linux)
        li $a2, 0                    # mode (ignored for O_RDONLY)
        syscall
        bltz $v0, open_failed
        sw $v0, -8($fp)              # fd

read_loop:
        # n = read(fd, buf, 256);
        li $v0, 14
        lw $a0, -8($fp)              # fd
        la $a1, buf
        li $a2, 256
        syscall
        blez $v0, close_and_exit     # 0 = EOF, <0 = error
        sw $v0, -12($fp)             # n

        # write(STDOUT, buf, n);
        li $v0, 15
        li $a0, 1                    # fd = stdout
        la $a1, buf
        lw $a2, -12($fp)             # n
        syscall

        j read_loop

close_and_exit:
        # close(fd);
        li $v0, 16
        lw $a0, -8($fp)              # fd
        syscall
        # exit2(1)  (see NOTES)
        li $a0, 1
        li $v0, 17
        syscall

open_failed:
        # print the fallback string, then exit2(1)
        li $v0, 4
        la $a0, altMessage
        syscall
        li $a0, 1
        li $v0, 17
        syscall
