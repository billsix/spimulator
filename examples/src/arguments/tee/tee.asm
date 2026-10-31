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


# C source — see tee.c
# Invocation:
#   echo hello | spimulator -f tee.asm a.txt b.txt


#PURPOSE:  Read stdin block-at-a-time, write each block to stdout
#          AND to every file named on the command line.  Capstone
#          of Phase C: variable argc, an fd array, and a per-block
#          fan-out write loop.
#
#NOTES:    Flags 577 = O_WRONLY|O_CREAT|O_TRUNC =
#          1 | 0x040 | 0x200 on Linux x86_64 (spim hands the flags
#          straight to the host's `open()`, so the host's
#          <fcntl.h> values are what matter here — NOT the values
#          a real MIPS-Linux kernel would expect, which differ).
#          Mode 420 = octal 0644 (rw-r--r--).
#
#          MAX_OUT = 8.  More than 8 named files is a usage error
#          rather than silent truncation of the file list.
#
#          STACK-VISIBLE STYLE: every C local lives in main's stack
#          frame — including the array `int fds[8]`, which occupies a
#          contiguous 32-byte region of the frame (fds[0] at the
#          lowest address, &fds[k] = ($fp-60) + 4*k).  `static char
#          buf[4096]` is the one exception: C `static` gives it
#          program-lifetime storage, so it stays in .data, not the
#          frame.  Scratch registers hold only working copies.  This
#          demo issues NO `jal` (every call is a syscall, which
#          preserves $ra), so it saves $fp but not $ra.  (The old
#          version parked all of this in callee-save $s* registers
#          and the array in .data; the frame is now the lesson.)
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr   | register   | C type     | bytes | role / notes
#  ------------+---------------+------------+------------+-------+---------------------------
#  argc        | -8($fp)       | $t1 (copy) | int        |    4  | arrives in $a0; homed, reloaded to test
#  argv        | -12($fp)      | $t1 (copy) | char **    |    4  | arrives in $a1; argv[i] fetched per open
#  nfds        | -16($fp)      | $t0 (copy) | int        |    4  | count of open fds (also next fds[] slot)
#  i           | -20($fp)      | $t0 (copy) | int        |    4  | open-loop index 1..argc-1
#  n           | -24($fp)      | $t0 (copy) | long       |    4  | bytes from the last read; sign -> exit status
#  j           | -28($fp)      | $t0 (copy) | int        |    4  | fan/close-loop index 0..nfds-1
#  fds[8]      | -60..-32($fp) | $t0 (addr) | int[8]     |   32  | open() results; fds[0] at -60, fds[7] at -32
#  buf[4096]   | buf (.data)   | —          | char[4096] | 4096  | static read buffer (C `static` -> .data)
#  (saved $fp) | -4($fp)       | —          | frame ptr  |    4  | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (illustrative, hand-traced from `echo hello | tee a.txt
#            b.txt`; in fan_loop, first block read, writing it to fds[0])
#
#          main's stack frame                scratch
#     +----------------------------+        +----------------------+
#     | -4($fp)  saved $fp         |        | $t0 = 0 (copy of j)
#     | -8($fp)  argc     =    3   |        | $t1 = 2 (copy of nfds)
#     | -12($fp) argv     = &av[0] |        +----------------------+
#     | -16($fp) nfds     =    2   |
#     | -20($fp) i        =    3   |   (open loop done: i == argc)
#     | -24($fp) n        =    6   |   ("hello\n" is 6 bytes)
#     | -28($fp) j        =    0   | <----- | $t0
#     | -32($fp) fds[7]   =    ?   |
#     |   ... (fds[2..6] unused) . |
#     | -56($fp) fds[1]   =    4   |   (b.txt)
#     | -60($fp) fds[0]   =    3   |   (a.txt)
#     +----------------------------+
#           $sp = $fp - 60
#
#   buf (in .data) holds the block; it is written to stdout, then to
#   fds[0] and fds[1].  At EOF the files are closed and tee exits 0.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  (The anchor uses stdout-only mode — no file arguments — so the
#   checker creates no files; the SNAPSHOT above is hand-traced from
#   the file-writing invocation in its header.)
#  stdin: arguments/tee/tee.input
#  stdout:
#  | hello
#  | world

        .data
usageMsg:   .asciiz "tee: too many output files\n"
openErr1:   .asciiz "tee: cannot open "
newline:    .asciiz "\n"
buf:        .space 4096                  # static char buf[4096] (C `static`)

        .text
        .globl main
main:
        # ---- prologue: build main's 60-byte frame (no $ra: no jal) -
        addiu $sp, $sp, -60          # allocate the frame
        sw $fp, 56($sp)              # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 60           # $fp = top of our frame
        sw $a0, -8($fp)              # argc = $a0
        sw $a1, -12($fp)             # argv = $a1

        # if (argc - 1 > MAX_OUT) too_many;
        lw $t0, -8($fp)              # $t0 = argc
        addi $t0, $t0, -1            # argc - 1
        li $t1, 8
        bgt $t0, $t1, too_many

        # for (i = 1, nfds = 0; i < argc; i++) fds[nfds++] = open(argv[i]);
        li $t0, 1
        sw $t0, -20($fp)             # i = 1   (skip argv[0])
        li $t0, 0
        sw $t0, -16($fp)             # nfds = 0

open_loop:
        lw $t0, -20($fp)             # $t0 = i
        lw $t1, -8($fp)              # $t1 = argc
        bge $t0, $t1, open_done

        # fd = os_open(argv[i], O_WRONLY|O_CREAT|O_TRUNC, 0644);
        lw $t1, -12($fp)             # $t1 = argv
        sll $t0, $t0, 2              # i * 4
        add $t0, $t1, $t0            # &argv[i]
        lw $a0, ($t0)                # argv[i]
        li $v0, 13                   # 13 = open
        li $a1, 577                  # O_WRONLY|O_CREAT|O_TRUNC
        li $a2, 420                  # mode 0644
        syscall                      # $v0 = fd (negative on error)
        bltz $v0, open_failed

        # fds[nfds] = fd;
        lw $t0, -16($fp)             # $t0 = nfds
        sll $t0, $t0, 2              # nfds * 4
        addiu $t1, $fp, -60          # $t1 = &fds[0]
        add $t0, $t1, $t0            # &fds[nfds]
        sw $v0, ($t0)                # fds[nfds] = fd

        # nfds++;
        lw $t0, -16($fp)
        addi $t0, $t0, 1
        sw $t0, -16($fp)
        # i++;
        lw $t0, -20($fp)
        addi $t0, $t0, 1
        sw $t0, -20($fp)
        j open_loop

open_done:
read_loop:
        # n = os_read(STDIN, buf, 4096);
        li $v0, 14
        li $a0, 0                    # fd = stdin
        la $a1, buf
        li $a2, 4096
        syscall
        sw $v0, -24($fp)             # n = bytes read
        lw $t0, -24($fp)
        blez $t0, close_all          # 0 = EOF, <0 = error

        # os_write(STDOUT, buf, n);
        li $v0, 15
        li $a0, 1
        la $a1, buf
        lw $a2, -24($fp)             # n
        syscall

        # for (j = 0; j < nfds; j++) os_write(fds[j], buf, n);
        li $t0, 0
        sw $t0, -28($fp)             # j = 0
fan_loop:
        lw $t0, -28($fp)             # $t0 = j
        lw $t1, -16($fp)             # $t1 = nfds
        bge $t0, $t1, read_loop
        sll $t0, $t0, 2              # j * 4
        addiu $t1, $fp, -60          # &fds[0]
        add $t0, $t1, $t0            # &fds[j]
        lw $a0, ($t0)                # fds[j]
        li $v0, 15
        la $a1, buf
        lw $a2, -24($fp)             # n
        syscall
        # j++;
        lw $t0, -28($fp)
        addi $t0, $t0, 1
        sw $t0, -28($fp)
        j fan_loop

close_all:
        # for (j = 0; j < nfds; j++) os_close(fds[j]);
        li $t0, 0
        sw $t0, -28($fp)             # j = 0
close_loop:
        lw $t0, -28($fp)             # $t0 = j
        lw $t1, -16($fp)             # $t1 = nfds
        bge $t0, $t1, exit_decide
        sll $t0, $t0, 2              # j * 4
        addiu $t1, $fp, -60          # &fds[0]
        add $t0, $t1, $t0            # &fds[j]
        lw $a0, ($t0)                # fds[j]
        li $v0, 16                   # 16 = close
        syscall
        # j++;
        lw $t0, -28($fp)
        addi $t0, $t0, 1
        sw $t0, -28($fp)
        j close_loop

exit_decide:
        # return n < 0 ? 1 : 0;
        lw $t0, -24($fp)             # $t0 = n
        bltz $t0, exit_err
        # ---- clean EOF: tear down the frame, return to the runtime -
        li $v0, 0                    # exit status 0
        lw $fp, -4($fp)              # restore caller's $fp
        addiu $sp, $sp, 60           # pop the frame
        jr $ra                       # $ra untouched (we made no `jal`)
exit_err:
        li $a0, 1
        li $v0, 17                   # exit(1) on read error
        syscall

open_failed:
        li $v0, 4
        la $a0, openErr1
        syscall
        # print_string(argv[i]);   (the one that failed to open)
        lw $t1, -12($fp)             # argv
        lw $t0, -20($fp)             # i
        sll $t0, $t0, 2
        add $t0, $t1, $t0            # &argv[i]
        lw $a0, ($t0)                # argv[i]
        li $v0, 4
        syscall
        li $v0, 4
        la $a0, newline
        syscall
        li $a0, 1
        li $v0, 17                   # exit(1)
        syscall

too_many:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit(1)
        syscall
