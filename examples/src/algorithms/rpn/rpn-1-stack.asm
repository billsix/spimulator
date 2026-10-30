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


# C source — see rpn-1-stack.c

# Invocation (whitespace-separated integers on stdin; prints the top at EOF):
#   printf '3 4 5\n' | spimulator -f rpn-1-stack.asm   ->  5

# PURPOSE:  Rung 1 of the RPN ladder — push whitespace-separated integers on an
#           evaluation stack, print the top at EOF.  No operators; integers (the
#           FPU arrives at rpn-3-float); well-formed input assumed (errors are
#           the final rpn).  The operand stack IS the MIPS $sp stack
#           (push = addi $sp,-4 + sw); depth in $s2, lookahead char in $s3.

# SYMBOL TABLE  (C variable -> MIPS location)
#   depth -> $s2 ; c (lookahead) -> $s3 ; saved $ra -> $s0 ; stack base -> $s1
#   a parsed integer -> $t2

        .text
        .globl main
main:
        move $s0, $ra                # save return address
        move $s1, $sp                # save original $sp (operand stack base)
        li   $s2, 0                  # depth = 0
        li   $v0, 12                 # c = read_char()
        syscall
        move $s3, $v0

read_loop:
        li   $t0, -1
        beq  $s3, $t0, at_eof         # while (c != EOF)
        li   $t0, ' '
        beq  $s3, $t0, skip_ws
        li   $t0, 10
        beq  $s3, $t0, skip_ws
        li   $t0, 9
        beq  $s3, $t0, skip_ws
        li   $t0, 13
        beq  $s3, $t0, skip_ws
        li   $t0, '0'                # digit? -> parse a number
        blt  $s3, $t0, skip_other
        li   $t0, '9'
        bgt  $s3, $t0, skip_other
        j    parse_number

skip_ws:
        li   $v0, 12
        syscall
        move $s3, $v0
        j    read_loop
skip_other:                          # well-formed input assumed: skip anything else
        li   $v0, 12
        syscall
        move $s3, $v0
        j    read_loop

# ---- parse an integer into $t2, then push it ----
parse_number:
        li   $t2, 0                  # value = 0
        li   $t3, 10
pn_loop:
        li   $t0, '0'
        blt  $s3, $t0, pn_push
        li   $t0, '9'
        bgt  $s3, $t0, pn_push
        addi $t0, $s3, -48           # digit = c - '0'
        mul  $t2, $t2, $t3           # value = value * 10 + digit
        add  $t2, $t2, $t0
        li   $v0, 12
        syscall
        move $s3, $v0
        j    pn_loop
pn_push:
        addi $sp, $sp, -4            # push the integer (4 bytes)
        sw   $t2, 0($sp)
        addi $s2, $s2, 1             # depth++
        j    read_loop               # c already holds the lookahead

# ---- end of input: print the top of stack ----
at_eof:
        lw   $a0, 0($sp)             # top of stack
        li   $v0, 1                  # print_int
        syscall
        li   $v0, 11                 # '\n'
        li   $a0, 10
        syscall
        move $sp, $s1                # restore original $sp
        move $ra, $s0
        li   $v0, 0
        jr   $ra
