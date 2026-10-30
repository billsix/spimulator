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


# C source — see calc-4-parens.c

# Invocation (one expression per line on stdin):
#   printf '(2+3)*4\n' | spimulator -f calc-4-parens.asm   ->  20

# PURPOSE:  Rung 4 of the calculator ladder — parentheses for grouping.
#           expr := term (('+'|'-') term)*, term := factor (('*'|'/') factor)*,
#           factor := DIGITS | '(' expr ')'.  factor and expr are MUTUALLY
#           RECURSIVE via '(' expr ')', so the operand is saved on the stack
#           across the recursive parse_expr() call.  Integers; no spaces; no
#           error path (well-formed input assumed).

# SYMBOL TABLE  (C name -> MIPS location)
#   cur (lookahead char) -> word at `curchar` (.data)
#   each routine's accumulator / saved value -> 0($sp) in its own frame

        .data
curchar:    .word 0

        .text
        .globl main
main:
        move $s0, $ra                # save return address
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
        move $ra, $s0
        li   $v0, 0
        jr   $ra


# ---------- advance() — cur = read_char() ----------
advance:
        li   $v0, 12
        syscall
        sw   $v0, curchar
        jr   $ra


# ---------- parse_factor() -> $v0   factor := DIGITS | '(' expr ')' ----------
#   frame: 0($sp)=saved value (parens case), 4($sp)=$ra
parse_factor:
        addi $sp, $sp, -8
        sw   $ra, 4($sp)
        lw   $t0, curchar
        li   $t1, '('
        bne  $t0, $t1, pf_number
        jal  advance                 # consume '('
        jal  parse_expr              # value -> $v0
        sw   $v0, 0($sp)             # hold it across the ')' handling
        lw   $t0, curchar
        li   $t1, ')'
        bne  $t0, $t1, pf_ret        # well-formed input assumed
        jal  advance                 # consume ')'
pf_ret:
        lw   $v0, 0($sp)
        lw   $ra, 4($sp)
        addi $sp, $sp, 8
        jr   $ra
pf_number:
        jal  parse_number            # $v0 = the number
        lw   $ra, 4($sp)
        addi $sp, $sp, 8
        jr   $ra


# ---------- parse_term() -> $v0   term := factor (('*'|'/') factor)* ----------
#   frame: 0($sp)=accumulator, 4($sp)=$ra
parse_term:
        addi $sp, $sp, -8
        sw   $ra, 4($sp)
        jal  parse_factor
        sw   $v0, 0($sp)
pt_loop:
        lw   $t0, curchar
        li   $t1, '*'
        beq  $t0, $t1, pt_mul
        li   $t1, '/'
        beq  $t0, $t1, pt_div
        lw   $v0, 0($sp)
        lw   $ra, 4($sp)
        addi $sp, $sp, 8
        jr   $ra
pt_mul:
        jal  advance
        jal  parse_factor
        lw   $t2, 0($sp)
        mul  $t2, $t2, $v0
        sw   $t2, 0($sp)
        j    pt_loop
pt_div:
        jal  advance
        jal  parse_factor
        lw   $t2, 0($sp)
        div  $t2, $t2, $v0           # integer, truncating
        sw   $t2, 0($sp)
        j    pt_loop


# ---------- parse_expr() -> $v0   expr := term (('+'|'-') term)* ----------
#   frame: 0($sp)=accumulator, 4($sp)=$ra
parse_expr:
        addi $sp, $sp, -8
        sw   $ra, 4($sp)
        jal  parse_term
        sw   $v0, 0($sp)
pe_loop:
        lw   $t0, curchar
        li   $t1, '+'
        beq  $t0, $t1, pe_add
        li   $t1, '-'
        beq  $t0, $t1, pe_sub
        lw   $v0, 0($sp)
        lw   $ra, 4($sp)
        addi $sp, $sp, 8
        jr   $ra
pe_add:
        jal  advance
        jal  parse_term
        lw   $t2, 0($sp)
        add  $t2, $t2, $v0
        sw   $t2, 0($sp)
        j    pe_loop
pe_sub:
        jal  advance
        jal  parse_term
        lw   $t2, 0($sp)
        sub  $t2, $t2, $v0
        sw   $t2, 0($sp)
        j    pe_loop


# ---------- parse_number() -> $v0  (DIGITS; assumes at least one digit) ----------
parse_number:
        addi $sp, $sp, -4
        sw   $ra, 0($sp)
        li   $t2, 0                  # value = 0
        li   $t3, 10
pn_loop:
        lw   $t0, curchar
        li   $t1, '0'
        blt  $t0, $t1, pn_done
        li   $t1, '9'
        bgt  $t0, $t1, pn_done
        addi $t0, $t0, -48
        mul  $t2, $t2, $t3
        add  $t2, $t2, $t0
        jal  advance
        j    pn_loop
pn_done:
        move $v0, $t2
        lw   $ra, 0($sp)
        addi $sp, $sp, 4
        jr   $ra
