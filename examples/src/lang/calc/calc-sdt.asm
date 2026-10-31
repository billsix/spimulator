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


# C source — see calc-sdt.c


# Invocations (one infix expression per line on stdin):
#   echo '(3 + 4) * 2 - 10 / 4' | spimulator -f calc-sdt.asm   ->  11.500000
#   echo '2 + 3 * 4'           | spimulator -f calc-sdt.asm   ->  14
#   echo '-5 + 3'              | spimulator -f calc-sdt.asm   ->  -2


#PURPOSE:  A TI-83-style infix calculator by syntax-directed translation:
#          recursive descent that EVALUATES WHILE PARSING (no tree).
#            expr   := term   (('+'|'-') term)*
#            term   := factor (('*'|'/') factor)*
#            factor := NUMBER | '-' factor | '(' expr ')'
#
#          STACK-VISIBLE STYLE: `cur` (lookahead) and `had_error` are C `static`
#          GLOBALS in .data.  expr and factor are MUTUALLY RECURSIVE, so the
#          operand a routine holds across a recursive call — a DOUBLE — lives at
#          -16($fp), an 8-byte 8-aligned slot read/written with ldc1/sdc1: the
#          actual numeric values are visibly on the stack between calls (the
#          return rides up in $f0).  parse_number's fractional_digits/seen_digit
#          are transient parse-internal counters in $t2/$t3 (the leaf advance
#          preserves them).  Every routine that `jal`s saves $ra.  skip_blanks
#          is a helper; print_double is a shared display helper.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE: cur -> curchar (.data, GLOBAL); had_error -> errflag (.data,
#  GLOBAL).  In parse_expr / parse_term / parse_factor the running DOUBLE is the
#  8-byte slot at -16($fp) (working copy in $f0, lhs loaded to $f2); saved $ra at
#  -4($fp), saved $fp at -8($fp).  advance()/skip_blanks() are helpers.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: lang/calc/calc-sdt.input
#  stdout:
#  | 11.500000
#  | 14
#  | -2
#  | 2.500000
#  | -10
#  | error

        .data
curchar:    .word 0
errflag:    .word 0
errMsg:     .asciiz "error"
nanMsg:     .asciiz "nan"
infMsg:     .asciiz "inf"

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

        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, 10                 # '\n' : blank line
        bne  $t0, $t1, ml_not_blank
        li   $v0, 12                 # consume the newline, next line
        syscall
        sw   $v0, curchar
        j    main_loop
ml_not_blank:
        li   $t1, -1
        beq  $t0, $t1, main_done

        sw   $zero, errflag          # had_error = 0
        jal  parse_expr              # value -> $f0 (FP preserved across the
                                     # integer-only skip_blanks below)
        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, 10
        beq  $t0, $t1, ml_check
        li   $t1, -1
        beq  $t0, $t1, ml_check
        li   $t2, 1                  # trailing garbage after a full expression
        sw   $t2, errflag
ml_check:
        lw   $t0, errflag
        beqz $t0, ml_print

        li   $v0, 4                  # print "error"
        la   $a0, errMsg
        syscall
ml_recover:                          # discard the rest of the bad line
        lw   $t0, curchar
        li   $t1, 10
        beq  $t0, $t1, ml_eol
        li   $t1, -1
        beq  $t0, $t1, ml_eol
        li   $v0, 12
        syscall
        sw   $v0, curchar
        j    ml_recover
ml_print:
        mov.d $f12, $f0
        jal  print_double
ml_eol:
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


# ---------- skip_blanks() — skip ' ' '\t' '\r' (NOT '\n') ----------
skip_blanks:
        addiu $sp, $sp, -8
        sw   $ra, 4($sp)             # -> -4($fp)
        sw   $fp, 0($sp)             # -> -8($fp)
        addiu $fp, $sp, 8
sb_loop:
        lw   $t0, curchar
        li   $t1, ' '
        beq  $t0, $t1, sb_adv
        li   $t1, 9
        beq  $t0, $t1, sb_adv
        li   $t1, 13
        beq  $t0, $t1, sb_adv
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 8
        jr   $ra
sb_adv:
        jal  advance
        j    sb_loop


# ---------- parse_factor() -> $f0   factor := NUMBER | '-' factor | '(' expr ')' ----------
#   -16($fp) = held double value (parens case, 8 bytes via sdc1/ldc1)
parse_factor:
        addiu $sp, $sp, -16
        sw   $ra, 12($sp)            # -> -4($fp)
        sw   $fp, 8($sp)             # -> -8($fp)
        addiu $fp, $sp, 16
        jal  skip_blanks
        lw   $t0, curchar

        li   $t1, '-'                # unary minus
        bne  $t0, $t1, pf_not_neg
        jal  advance
        jal  parse_factor
        neg.d $f0, $f0
        j    pf_ret
pf_not_neg:
        li   $t1, '('                # parenthesized expr
        bne  $t0, $t1, pf_not_paren
        jal  advance
        jal  parse_expr
        sdc1 $f0, -16($fp)           # hold the value across the ')' handling
        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, ')'
        bne  $t0, $t1, pf_paren_err
        jal  advance
        ldc1 $f0, -16($fp)
        j    pf_ret
pf_paren_err:
        li   $t0, 1                  # unbalanced parenthesis
        sw   $t0, errflag
        ldc1 $f0, -16($fp)
        j    pf_ret
pf_not_paren:
        li   $t1, '.'                # NUMBER starts with a digit or '.'
        beq  $t0, $t1, pf_number
        li   $t1, '0'
        blt  $t0, $t1, pf_bad
        li   $t1, '9'
        bgt  $t0, $t1, pf_bad
pf_number:
        mtc1 $zero, $f0
        cvt.d.w $f0, $f0             # mantissa = 0.0
        li   $t1, 10
        mtc1 $t1, $f4
        cvt.d.w $f4, $f4             # ten = 10.0
        li   $t2, 0                  # fractional_digits (transient; advance preserves $t2)
        li   $t3, 0                  # seen_digit (transient; advance preserves $t3)
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
        li   $t3, 1
        jal  advance
        j    pfn_int
pfn_after_int:
        lw   $t0, curchar
        li   $t1, '.'
        bne  $t0, $t1, pfn_scale
        jal  advance
pfn_frac:
        lw   $t0, curchar
        li   $t1, '0'
        blt  $t0, $t1, pfn_scale
        li   $t1, '9'
        bgt  $t0, $t1, pfn_scale
        addi $t0, $t0, -48
        mtc1 $t0, $f2
        cvt.d.w $f2, $f2
        mul.d $f0, $f0, $f4
        add.d $f0, $f0, $f2
        addi $t2, $t2, 1
        li   $t3, 1
        jal  advance
        j    pfn_frac
pfn_scale:
        bnez $t3, pfn_div            # at least one digit?
        li   $t0, 1                  # lone '.' -> error, value 0
        sw   $t0, errflag
        mtc1 $zero, $f0
        cvt.d.w $f0, $f0
        j    pf_ret
pfn_div:
        blez $t2, pf_ret             # value = mantissa / 10^fractional_digits
        div.d $f0, $f0, $f4
        addi $t2, $t2, -1
        j    pfn_div
pf_bad:
        li   $t0, 1                  # unexpected char where a factor was wanted
        sw   $t0, errflag
        mtc1 $zero, $f0
        cvt.d.w $f0, $f0
pf_ret:
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 16
        jr   $ra


# ---------- parse_term() -> $f0   term := factor (('*'|'/') factor)* ----------
#   -16($fp) = accumulator (double, 8 bytes via sdc1/ldc1)
parse_term:
        addiu $sp, $sp, -16
        sw   $ra, 12($sp)            # -> -4($fp)
        sw   $fp, 8($sp)             # -> -8($fp)
        addiu $fp, $sp, 16
        jal  parse_factor
pt_loop:
        sdc1 $f0, -16($fp)           # save accumulator across the next call
        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, '*'
        beq  $t0, $t1, pt_mul
        li   $t1, '/'
        beq  $t0, $t1, pt_div
        ldc1 $f0, -16($fp)           # neither: restore and return
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 16
        jr   $ra
pt_mul:
        jal  advance
        jal  parse_factor            # rhs -> $f0
        ldc1 $f2, -16($fp)           # lhs
        mul.d $f0, $f2, $f0
        j    pt_loop
pt_div:
        jal  advance
        jal  parse_factor
        ldc1 $f2, -16($fp)
        div.d $f0, $f2, $f0
        j    pt_loop


# ---------- parse_expr() -> $f0   expr := term (('+'|'-') term)* ----------
#   -16($fp) = accumulator (double, 8 bytes via sdc1/ldc1)
parse_expr:
        addiu $sp, $sp, -16
        sw   $ra, 12($sp)            # -> -4($fp)
        sw   $fp, 8($sp)             # -> -8($fp)
        addiu $fp, $sp, 16
        jal  parse_term
pe_loop:
        sdc1 $f0, -16($fp)
        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, '+'
        beq  $t0, $t1, pe_add
        li   $t1, '-'
        beq  $t0, $t1, pe_sub
        ldc1 $f0, -16($fp)
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 16
        jr   $ra
pe_add:
        jal  advance
        jal  parse_term
        ldc1 $f2, -16($fp)
        add.d $f0, $f2, $f0
        j    pe_loop
pe_sub:
        jal  advance
        jal  parse_term
        ldc1 $f2, -16($fp)
        sub.d $f0, $f2, $f0
        j    pe_loop


# ---------- print_double($f12) — fixed deterministic format (shared display
#            helper; leaf, register-based; see rpn.asm) ----------
#   nan -> "nan"; sign; inf -> "inf"; else integer part, and if a fraction
#   remains, '.' + 6 truncated fractional digits.
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
