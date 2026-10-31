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


# C source — see wc.c
# Invocations:
#   spimulator -f wc.asm             # reads stdin
#   spimulator -f wc.asm -           # reads stdin (explicit "-")
#   spimulator -f wc.asm /etc/motd   # reads the file


#PURPOSE:  wc -cl with real-Unix argv handling.  No file arg (or
#          explicit "-") -> stdin.  Filename arg -> open and read.
#
#NOTES:    Uses syscall 14 (read fd into buffer) so the same loop
#          body works for both stdin and a file fd.  Reads 1 byte at
#          a time; a real wc would block-buffer, but the byte loop
#          matches the demo's pedagogical shape.
#
#          STACK-VISIBLE STYLE: fd, byte_count, line_count, file_arg
#          and the read byte `c` all live in stack slots.  `c` is a
#          genuine stack local whose ADDRESS (&c) is handed to read —
#          `addiu $a1,$fp,-24` computes that address.  A scratch
#          register ($t0/$t1) holds a working copy only while an
#          instruction needs it.  This routine makes no `jal` (only
#          syscalls, which preserve $ra), so it saves no $ra.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable   | memory addr | register   | C type    | bytes | role / notes
#  -------------+-------------+------------+-----------+-------+-------------------------
#  argc         | —           | $a0        | int       |   4   | entry only; argc dispatch
#  argv         | —           | $a1        | char **   |   4   | entry only; argv[1] read
#  fd           | -8($fp)     | $t0 (copy) | int       |   4   | 0 = STDIN, else open()'s return
#  byte_count   | -12($fp)    | $t0 (copy) | int       |   4   | running tally (every byte)
#  line_count   | -16($fp)    | $t0 (copy) | int       |   4   | running tally (newlines only)
#  file_arg     | -20($fp)    | $t1 (copy) | char *    |   4   | argv[1] (name or "-"); 0 if none
#  c            | -24($fp)    | —          | char      |   1   | read landing pad; &c passed to read
#  n (read ret) | —           | $v0        | long      |   4   | bytes read this call; tested, transient
#  (saved $fp)  | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#  (fd, byte_count and line_count each use $t0 as their scratch copy
#   at different times — $t0 is reused, never holding two at once.)
#
# ------------------------------------------------------------------
#  SNAPSHOT (input: piped wc.input, just after the first '\n' is
#            counted — the end of "hello world\n")
#
#        main's stack frame                 scratch
#   +-----------------------------+        +-------------------------+
#   | -4($fp)  saved $fp          |        | $t0 = 1 (line_count, ++'d)
#   | -8($fp)  fd          =  0   |        +-------------------------+
#   | -12($fp) byte_count  = 12   |
#   | -16($fp) line_count  =  1   |
#   | -20($fp) file_arg    =  0   |   (stdin: no filename)
#   | -24($fp) c           ='\n'  |
#   +-----------------------------+
#         $sp = $fp - 24
#
#   The loop reads one byte into c's slot, does byte_count++ always
#   and line_count++ only on '\n'.  wc.input is 39 bytes with 3
#   newlines (its last line has no trailing '\n'), so the program
#   ends by printing "39 bytes, 3 lines".
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: transforms/wc/wc.input
#  stdout:
#  | 39 bytes, 3 lines

        .data
usageMsg:   .asciiz "usage: wc [FILE|-]\n"
errMsg:     .asciiz "wc: cannot open "
nlMsg:      .asciiz "\n"
bytesText:  .asciiz " bytes, "
linesText:  .asciiz " lines\n"

        .text
        .globl main
main:
        # ---- prologue: build main's 24-byte frame (no $ra: no jal) -
        addiu $sp, $sp, -24          # allocate the frame
        sw $fp, 20($sp)              # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 24           # $fp = top of our frame

        sw $0, -8($fp)               # fd = STDIN (0)
        sw $0, -20($fp)              # file_arg = NULL

        # argc dispatch   ($a0 = argc, $a1 = argv)
        li $t0, 1
        beq $a0, $t0, read_setup     # argc == 1 -> stdin
        li $t0, 2
        bgt $a0, $t0, usage          # argc > 2 -> usage

        # argc == 2: inspect argv[1]
        lw $t1, 4($a1)               # $t1 = argv[1] address
        sw $t1, -20($fp)             # file_arg = argv[1]
        lb $t0, 0($t1)
        bne $t0, '-', do_open        # doesn't start with '-' -> filename
        lb $t0, 1($t1)
        beq $t0, $0, read_setup      # argv[1] is exactly "-" -> stdin

do_open:
        # fd = open(file_arg, O_RDONLY, 0)
        lw $a0, -20($fp)             # file_arg
        li $v0, 13                   # 13 = open
        li $a1, 0                    # O_RDONLY
        li $a2, 0                    # mode (ignored)
        syscall
        bltz $v0, open_failed
        sw $v0, -8($fp)              # fd = open's return

read_setup:
        sw $0, -12($fp)              # byte_count = 0
        sw $0, -16($fp)              # line_count = 0

read_loop:
        # n = read(fd, &c, 1)
        li $v0, 14                   # 14 = read
        lw $a0, -8($fp)              # fd
        addiu $a1, $fp, -24          # &c  (address of c's stack slot)
        li $a2, 1
        syscall
        blez $v0, read_done          # 0 = EOF, <0 = error

        lw $t0, -12($fp)             # byte_count
        addi $t0, $t0, 1
        sw $t0, -12($fp)             # byte_count++
        lb $t0, -24($fp)             # c
        bne $t0, '\n', read_loop
        lw $t0, -16($fp)             # line_count
        addi $t0, $t0, 1
        sw $t0, -16($fp)             # line_count++
        j read_loop

read_done:
        # close fd if it's not STDIN
        lw $t0, -8($fp)              # fd
        beqz $t0, print_results
        li $v0, 16                   # 16 = close
        move $a0, $t0
        syscall

print_results:
        # print_int(byte_count)
        lw $a0, -12($fp)
        li $v0, 1
        syscall

        # print_string(" bytes, ")
        li $v0, 4
        la $a0, bytesText
        syscall

        # print_int(line_count)
        lw $a0, -16($fp)
        li $v0, 1
        syscall

        # print_string(" lines\n")
        li $v0, 4
        la $a0, linesText
        syscall

        # ---- epilogue: tear down the frame, return to the runtime --
        li $v0, 0                    # exit status 0
        lw $fp, -4($fp)              # restore caller's $fp
        addiu $sp, $sp, 24           # pop the frame
        jr $ra

open_failed:
        # print "wc: cannot open <argv[1]>\n"
        li $v0, 4
        la $a0, errMsg
        syscall
        lw $a0, -20($fp)             # file_arg
        li $v0, 4
        syscall
        li $v0, 4
        la $a0, nlMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall
