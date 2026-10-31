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


# C source — see calc-5-neg.c

# Invocation (one expression per line on stdin):
#   printf '-2*3\n' | spimulator -f calc-5-neg.asm   ->  -6

# PURPOSE:  Rung 5 of the calculator ladder — unary minus / negative numbers.
#           factor := DIGITS | '-' factor | '(' expr ')'.
#
#           STACK-VISIBLE STYLE: `cur` is a C `static` GLOBAL in .data.  Each
#           parse routine homes its running C local at -12($fp): the accumulator
#           (expr/term), the parenthesised value (factor), the digit value
#           (number).  The unary-minus path negates parse_factor's transient
#           result in $v0 (no slot needed).  A scratch register holds a working
#           COPY; every routine that `jal`s saves $ra.
#           Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE: cur -> curchar (.data, GLOBAL).  Running locals at -12($fp)
#  (working copy in $t2/$v0); saved $ra at -4($fp), saved $fp at -8($fp).
#  advance() is a leaf helper.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: lang/calc/calc-5-neg.input
#  stdout:
#  | -5
#  | -2
#  | -10
#  | -6

        .data
curchar:    .word 0

        .text
        .globl main
main:
        addiu $sp, $sp, -8
        sw   $ra, 4($sp)             # -> -4($fp)
        sw   $fp, 0($sp)             # -> -8($fp)
        addiu $fp, $sp, 8

        li   $v0, 12                 # prime the first lookahead char
        syscall
        sw   $v0, curchar

main_loop:
        lw   $t0, curchar
        li   $t1, -1
        beq  $t0, $t1, main_done     # EOF -> done
        li   $t1, 10                 # '\n' : blank line
        bne  $t0, $t1, ml_expr
        li   $v0, 12                 # consume the newline, next line
        syscall
        sw   $v0, curchar
        j    main_loop
ml_expr:
        jal  parse_expr              # value -> $v0
        move $a0, $v0
        li   $v0, 1                  # print_int(value)
        syscall
        li   $v0, 11                 # end the output line
        li   $a0, 10
        syscall
        lw   $t0, curchar
        li   $t1, 10
        bne  $t0, $t1, main_loop
        li   $v0, 12                 # consume the newline
        syscall
        sw   $v0, curchar
        j    main_loop

main_done:
        li   $v0, 0
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 8
        jr   $ra


# ---------- advance() — cur = read_char()  (leaf helper) ----------
advance:
        li   $v0, 12
        syscall
        sw   $v0, curchar
        jr   $ra


# ---------- parse_factor() -> $v0   factor := DIGITS | '-' factor | '(' expr ')' ----------
parse_factor:
        addiu $sp, $sp, -12
        sw   $ra, 8($sp)             # -> -4($fp)
        sw   $fp, 4($sp)             # -> -8($fp)
        addiu $fp, $sp, 12
        lw   $t0, curchar
        li   $t1, '-'                # unary minus
        bne  $t0, $t1, pf_not_neg
        jal  advance
        jal  parse_factor
        neg  $v0, $v0                # negate (additive inverse; transient in $v0)
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 12
        jr   $ra
pf_not_neg:
        li   $t1, '('
        bne  $t0, $t1, pf_number
        jal  advance                 # consume '('
        jal  parse_expr
        sw   $v0, -12($fp)           # hold across the ')' handling
        lw   $t0, curchar
        li   $t1, ')'
        bne  $t0, $t1, pf_ret        # well-formed input assumed
        jal  advance                 # consume ')'
pf_ret:
        lw   $v0, -12($fp)
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 12
        jr   $ra
pf_number:
        jal  parse_number
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 12
        jr   $ra


# ---------- parse_term() -> $v0   term := factor (('*'|'/') factor)* ----------
parse_term:
        addiu $sp, $sp, -12
        sw   $ra, 8($sp)             # -> -4($fp)
        sw   $fp, 4($sp)             # -> -8($fp)
        addiu $fp, $sp, 12
        jal  parse_factor
        sw   $v0, -12($fp)           # acc = first factor
pt_loop:
        lw   $t0, curchar
        li   $t1, '*'
        beq  $t0, $t1, pt_mul
        li   $t1, '/'
        beq  $t0, $t1, pt_div
        lw   $v0, -12($fp)
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 12
        jr   $ra
pt_mul:
        jal  advance
        jal  parse_factor
        lw   $t2, -12($fp)
        mul  $t2, $t2, $v0
        sw   $t2, -12($fp)
        j    pt_loop
pt_div:
        jal  advance
        jal  parse_factor
        lw   $t2, -12($fp)
        div  $t2, $t2, $v0           # integer, truncating
        sw   $t2, -12($fp)
        j    pt_loop


# ---------- parse_expr() -> $v0   expr := term (('+'|'-') term)* ----------
parse_expr:
        addiu $sp, $sp, -12
        sw   $ra, 8($sp)             # -> -4($fp)
        sw   $fp, 4($sp)             # -> -8($fp)
        addiu $fp, $sp, 12
        jal  parse_term
        sw   $v0, -12($fp)           # acc = first term
pe_loop:
        lw   $t0, curchar
        li   $t1, '+'
        beq  $t0, $t1, pe_add
        li   $t1, '-'
        beq  $t0, $t1, pe_sub
        lw   $v0, -12($fp)
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 12
        jr   $ra
pe_add:
        jal  advance
        jal  parse_term
        lw   $t2, -12($fp)
        add  $t2, $t2, $v0
        sw   $t2, -12($fp)
        j    pe_loop
pe_sub:
        jal  advance
        jal  parse_term
        lw   $t2, -12($fp)
        sub  $t2, $t2, $v0
        sw   $t2, -12($fp)
        j    pe_loop


# ---------- parse_number() -> $v0  (DIGITS; assumes at least one digit) ----------
parse_number:
        addiu $sp, $sp, -16
        sw   $ra, 12($sp)            # -> -4($fp)
        sw   $fp, 8($sp)             # -> -8($fp)
        addiu $fp, $sp, 16
        li   $t2, 0
        sw   $t2, -12($fp)           # value = 0
pn_loop:
        lw   $t0, curchar
        li   $t1, '0'
        blt  $t0, $t1, pn_done
        li   $t1, '9'
        bgt  $t0, $t1, pn_done
        addi $t0, $t0, -48           # digit = cur - '0'
        lw   $t2, -12($fp)           # value
        li   $t3, 10
        mul  $t2, $t2, $t3           # value * 10
        add  $t2, $t2, $t0           # + digit
        sw   $t2, -12($fp)           # store value back
        jal  advance
        j    pn_loop
pn_done:
        lw   $v0, -12($fp)           # return value
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 16
        jr   $ra
