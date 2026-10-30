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


# C source — see rpn-2-ops.c

# Invocation (whitespace-separated tokens on stdin; prints the top at EOF):
#   printf '3 4 + 2 * 1 3 / -\n' | spimulator -f rpn-2-ops.asm   ->  14

# PURPOSE:  Rung 2 of the RPN ladder — the four operators '+','-','*','/'.  An
#           operator pops the top two integers off the $sp stack, applies the op,
#           and pushes the result.  No precedence (RPN); integer '/' truncates
#           (1 3 / -> 0); no error path (well-formed input assumed).

# SYMBOL TABLE  (C variable -> MIPS location)
#   depth -> $s2 ; c (lookahead) -> $s3 ; saved $ra -> $s0 ; stack base -> $s1
#   a parsed integer -> $t2 ; left -> $t4 ; right -> $t5

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
        beq  $s3, $t0, at_eof
        li   $t0, ' '
        beq  $s3, $t0, skip_ws
        li   $t0, 10
        beq  $s3, $t0, skip_ws
        li   $t0, 9
        beq  $s3, $t0, skip_ws
        li   $t0, 13
        beq  $s3, $t0, skip_ws
        li   $t0, '0'                # digit? -> parse a number
        blt  $s3, $t0, check_op
        li   $t0, '9'
        bgt  $s3, $t0, check_op
        j    parse_number
check_op:
        li   $t0, '+'
        beq  $s3, $t0, do_op
        li   $t0, '-'
        beq  $s3, $t0, do_op
        li   $t0, '*'
        beq  $s3, $t0, do_op
        li   $t0, '/'
        beq  $s3, $t0, do_op
        j    skip_other              # well-formed input assumed

skip_ws:
        li   $v0, 12
        syscall
        move $s3, $v0
        j    read_loop
skip_other:
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
        addi $t0, $s3, -48
        mul  $t2, $t2, $t3
        add  $t2, $t2, $t0
        li   $v0, 12
        syscall
        move $s3, $v0
        j    pn_loop
pn_push:
        addi $sp, $sp, -4
        sw   $t2, 0($sp)
        addi $s2, $s2, 1             # depth++
        j    read_loop

# ---- apply a binary operator: pop two, apply, push one ----
do_op:
        lw   $t5, 0($sp)             # right = pop()
        addi $sp, $sp, 4
        lw   $t4, 0($sp)             # left  = pop()
        addi $sp, $sp, 4
        addi $s2, $s2, -2
        li   $t0, '+'
        beq  $s3, $t0, op_add
        li   $t0, '-'
        beq  $s3, $t0, op_sub
        li   $t0, '*'
        beq  $s3, $t0, op_mul
        div  $t4, $t4, $t5           # '/' (integer, truncating)
        j    op_store
op_add:
        add  $t4, $t4, $t5
        j    op_store
op_sub:
        sub  $t4, $t4, $t5
        j    op_store
op_mul:
        mul  $t4, $t4, $t5
op_store:
        addi $sp, $sp, -4
        sw   $t4, 0($sp)
        addi $s2, $s2, 1             # depth++
        li   $v0, 12
        syscall
        move $s3, $v0
        j    read_loop

# ---- end of input: print the top of stack ----
at_eof:
        lw   $a0, 0($sp)
        li   $v0, 1                  # print_int
        syscall
        li   $v0, 11
        li   $a0, 10                 # '\n'
        syscall
        move $sp, $s1
        move $ra, $s0
        li   $v0, 0
        jr   $ra
