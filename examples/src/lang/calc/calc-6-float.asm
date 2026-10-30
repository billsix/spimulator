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


# C source — see calc-6-float.c

# Invocation (one expression per line on stdin):
#   printf '10/4\n'          | spimulator -f calc-6-float.asm   ->  2.500000
#   printf '(3+4)*2-10/4\n'  | spimulator -f calc-6-float.asm   ->  11.500000

# PURPOSE:  Rung 6 of the calculator ladder — introduce the FPU.  Values are now
#           DOUBLES on $f0..$f31, numbers gain an optional '.'-fraction, and
#           every operator is a '.d' op, so '/' yields decimals (10/4 -> 2.5).
#           Same grammar as rung 5; display reuses rpn/calc-sdt's fixed printer.
#           This is calc-sdt minus skip_blanks and the error path (well-formed
#           input assumed); add those and it becomes calc-sdt.

# SYMBOL TABLE  (C name -> MIPS location)
#   cur (lookahead char) -> word at `curchar` (.data)
#   a parsed value -> $f0 (returned up the parse call chain)
#   a value held across a recursive call -> 0($sp) frame (sdc1/ldc1)

        .data
curchar:    .word 0
nanMsg:     .asciiz "nan"
infMsg:     .asciiz "inf"

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
        jal  parse_expr              # value -> $f0
        mov.d $f12, $f0
        jal  print_double
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


# ---------- parse_factor() -> $f0   factor := NUMBER | '-' factor | '(' expr ')' ----------
#   frame: 0($sp)=saved value (double, parens case), 8($sp)=$ra
parse_factor:
        addi $sp, $sp, -16
        sw   $ra, 8($sp)
        lw   $t0, curchar

        li   $t1, '-'                # unary minus
        bne  $t0, $t1, pf_not_neg
        jal  advance
        jal  parse_factor
        neg.d $f0, $f0
        j    pf_ret
pf_not_neg:
        li   $t1, '('                # parenthesized expr
        bne  $t0, $t1, pf_number
        jal  advance
        jal  parse_expr
        sdc1 $f0, 0($sp)             # hold the value across the ')' handling
        lw   $t0, curchar
        li   $t1, ')'
        bne  $t0, $t1, pf_paren_ret  # well-formed input assumed
        jal  advance
pf_paren_ret:
        ldc1 $f0, 0($sp)
        j    pf_ret
pf_number:
        mtc1 $zero, $f0
        cvt.d.w $f0, $f0             # mantissa = 0.0
        li   $t1, 10
        mtc1 $t1, $f4
        cvt.d.w $f4, $f4             # ten = 10.0
        li   $t2, 0                  # fractional_digits
pfn_int:
        lw   $t0, curchar
        li   $t1, '0'
        blt  $t0, $t1, pfn_after_int
        li   $t1, '9'
        bgt  $t0, $t1, pfn_after_int
        addi $t0, $t0, -48
        mtc1 $t0, $f2
        cvt.d.w $f2, $f2
        mul.d $f0, $f0, $f4
        add.d $f0, $f0, $f2
        jal  advance
        j    pfn_int
pfn_after_int:
        lw   $t0, curchar
        li   $t1, '.'
        bne  $t0, $t1, pfn_div
        jal  advance
pfn_frac:
        lw   $t0, curchar
        li   $t1, '0'
        blt  $t0, $t1, pfn_div
        li   $t1, '9'
        bgt  $t0, $t1, pfn_div
        addi $t0, $t0, -48
        mtc1 $t0, $f2
        cvt.d.w $f2, $f2
        mul.d $f0, $f0, $f4
        add.d $f0, $f0, $f2
        addi $t2, $t2, 1
        jal  advance
        j    pfn_frac
pfn_div:
        blez $t2, pf_ret             # value = mantissa / 10^fractional_digits
        div.d $f0, $f0, $f4
        addi $t2, $t2, -1
        j    pfn_div
pf_ret:
        lw   $ra, 8($sp)
        addi $sp, $sp, 16
        jr   $ra


# ---------- parse_term() -> $f0   term := factor (('*'|'/') factor)* ----------
#   frame: 0($sp)=accumulator (double), 8($sp)=$ra
parse_term:
        addi $sp, $sp, -16
        sw   $ra, 8($sp)
        jal  parse_factor
pt_loop:
        sdc1 $f0, 0($sp)             # save accumulator across the next call
        lw   $t0, curchar
        li   $t1, '*'
        beq  $t0, $t1, pt_mul
        li   $t1, '/'
        beq  $t0, $t1, pt_div
        ldc1 $f0, 0($sp)             # neither: restore and return
        lw   $ra, 8($sp)
        addi $sp, $sp, 16
        jr   $ra
pt_mul:
        jal  advance
        jal  parse_factor            # rhs -> $f0
        ldc1 $f2, 0($sp)             # lhs
        mul.d $f0, $f2, $f0
        j    pt_loop
pt_div:
        jal  advance
        jal  parse_factor
        ldc1 $f2, 0($sp)
        div.d $f0, $f2, $f0
        j    pt_loop


# ---------- parse_expr() -> $f0   expr := term (('+'|'-') term)* ----------
#   frame: 0($sp)=accumulator (double), 8($sp)=$ra
parse_expr:
        addi $sp, $sp, -16
        sw   $ra, 8($sp)
        jal  parse_term
pe_loop:
        sdc1 $f0, 0($sp)
        lw   $t0, curchar
        li   $t1, '+'
        beq  $t0, $t1, pe_add
        li   $t1, '-'
        beq  $t0, $t1, pe_sub
        ldc1 $f0, 0($sp)
        lw   $ra, 8($sp)
        addi $sp, $sp, 16
        jr   $ra
pe_add:
        jal  advance
        jal  parse_term
        ldc1 $f2, 0($sp)
        add.d $f0, $f2, $f0
        j    pe_loop
pe_sub:
        jal  advance
        jal  parse_term
        ldc1 $f2, 0($sp)
        sub.d $f0, $f2, $f0
        j    pe_loop


# ---------- print_double($f12) — fixed deterministic format (see rpn.asm / calc-sdt.asm) ----
#   nan -> "nan"; sign; inf -> "inf"; else integer part, and if a fraction
#   remains, '.' + 6 truncated fractional digits.  Leaf (only syscalls).
print_double:
        c.eq.d $f12, $f12            # nan is the only value != itself
        bc1t pd_not_nan
        li   $v0, 4
        la   $a0, nanMsg
        syscall
        jr   $ra
pd_not_nan:
        mtc1 $zero, $f14
        cvt.d.w $f14, $f14           # zero = 0.0
        c.lt.d $f12, $f14            # value < 0 ?
        bc1f pd_nonneg
        li   $v0, 11
        li   $a0, '-'
        syscall
        neg.d $f12, $f12
pd_nonneg:
        add.d $f16, $f12, $f12       # inf: value != 0 && value+value == value
        c.eq.d $f16, $f12
        bc1f pd_finite
        c.eq.d $f12, $f14
        bc1t pd_finite               # 0 is finite (prints "0")
        li   $v0, 4
        la   $a0, infMsg
        syscall
        jr   $ra
pd_finite:
        li   $t1, 10
        mtc1 $t1, $f18
        cvt.d.w $f18, $f18           # ten = 10.0
        trunc.w.d $f4, $f12          # integer_part = trunc(value)
        mfc1 $t0, $f4
        move $a0, $t0
        li   $v0, 1                  # print_int(integer_part)
        syscall
        cvt.d.w $f6, $f4             # (double)integer_part
        sub.d $f12, $f12, $f6        # fraction = value - integer_part
        c.eq.d $f12, $f14
        bc1t pd_done                 # integral -> done
        li   $v0, 11
        li   $a0, '.'
        syscall
        li   $t2, 6                  # FRAC_DIGITS
pd_frac_loop:
        blez $t2, pd_done
        mul.d $f12, $f12, $f18       # fraction *= 10
        trunc.w.d $f4, $f12          # digit = trunc(fraction)
        mfc1 $t0, $f4
        addi $a0, $t0, 48
        li   $v0, 11
        syscall
        cvt.d.w $f6, $f4
        sub.d $f12, $f12, $f6
        addi $t2, $t2, -1
        j    pd_frac_loop
pd_done:
        jr   $ra
