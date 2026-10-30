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

# PURPOSE:  Rung 1 of the head ladder — the filter only: copy the first 10 lines
#           of stdin to stdout, then stop.  No argv (fixed count, stdin only);
#           the finished head adds the -n flag + a FILE argument.  A stream-state
#           byte loop: read, echo, count newlines, stop at the limit.

# SYMBOL TABLE  (C name -> MIPS location)
#   line_count -> $t2 ; N -> $t3 ; c (byte read) -> $v0/$a0

        .text
        .globl main
main:
        move $s0, $ra
        li   $t2, 0                  # line_count = 0
        li   $t3, 10                 # N = 10
hc_loop:
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
        addi $t2, $t2, 1
        j    hc_loop
hc_done:
        move $ra, $s0
        li   $v0, 0
        jr   $ra
