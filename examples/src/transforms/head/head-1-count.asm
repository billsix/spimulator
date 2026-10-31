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


# C source — see head-1-count.c

# Invocation (prints the first 10 lines of stdin):
#   printf 'a\nb\n...\n' | spimulator -f head-1-count.asm

# PURPOSE:  Rung 1 of the head ladder — copy the first 10 lines of stdin to
#           stdout, then stop.  A stream-state byte loop: read, echo, count
#           newlines, stop at the limit.
#
#           STACK-VISIBLE STYLE: the C local `line_count` lives in a stack slot;
#           a scratch register holds only a working COPY.  The limit N is a
#           constant (immediate) and the byte `c` is a transient (read, echoed,
#           tested within one iteration).  This demo makes NO `jal` (only
#           syscalls, which preserve $ra), so it saves $fp but not $ra.
#           Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  line_count  | -8($fp)     | $t2 (copy) | int       |   4   | newlines echoed so far (stop at N=10)
#  (c)         | —           | $v0/$a0    | int       |   4   | transient byte (-1 at EOF)
#  (saved $fp) | -4($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: transforms/head/head-1-count.input
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

        .text
        .globl main
main:
        addiu $sp, $sp, -8
        sw   $fp, 4($sp)             # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 8

        li   $t2, 0
        sw   $t2, -8($fp)            # line_count = 0
hc_loop:
        lw   $t2, -8($fp)            # line_count
        li   $t3, 10                 # N = 10
        bge  $t2, $t3, hc_done       # while (line_count < N ...)
        li   $v0, 12                 # ... && (c = read_char()) != EOF
        syscall
        li   $t0, -1
        beq  $v0, $t0, hc_done
        move $a0, $v0                # print_char(c)
        li   $v0, 11
        syscall
        li   $t0, 10                 # if (c == '\n') line_count++
        bne  $a0, $t0, hc_loop
        lw   $t2, -8($fp)
        addi $t2, $t2, 1
        sw   $t2, -8($fp)
        j    hc_loop
hc_done:
        li   $v0, 0
        lw   $fp, -4($fp)
        addiu $sp, $sp, 8
        jr   $ra
