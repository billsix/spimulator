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


# C source — see calc-eval-test.c

# Invocation (no input; runs the test battery):
#   spimulator -f calc-eval-test.asm   ->  "<expr> => <result>" per case

# PURPOSE:  Unit tests for the I/O-separated calculator core.  Because the parser
#           walks a .data CHARACTER BUFFER (advance reads `cursor`, not stdin), it
#           can be driven by a table of hardcoded expression strings with no I/O.
#           main loops the `cases` table, printing "<expr> => <result>" for each.
#
#           STACK-VISIBLE STYLE: `cursor`, `cur`, `had_error` and the case table
#           are C `static` GLOBALS in .data.  main homes its two C locals — the
#           case-table pointer and the current expression string — at -12($fp)
#           and -16($fp); a scratch register holds only a working COPY.  The eval
#           core (advance/skip_blanks/parse_*/print_double) is calc-eval.asm's,
#           verbatim: the parse routines keep their running DOUBLE at the
#           8-aligned slot -16($fp).  Every routine that `jal`s saves $ra.
#           Style reference: tasks/reference/mips-asm-teaching-style.md
#
#  SYMBOL TABLE: cursor/cur/had_error/cases -> .data GLOBALS.  main's case
#  pointer -> -12($fp), current string -> -16($fp).  parse routines' running
#  DOUBLE -> -16($fp).  saved $ra/$fp at -4/-8($fp).
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args:
#  stdout:
#  | 2+3 => 5
#  | 10/4 => 2.500000
#  | (3+4)*2-10/4 => 11.500000
#  | -5+3 => -2
#  | 3*-2 => -6
#  | 2 + 3 * 4 => 14
#  | .5*2 => 1
#  | (2+3 => error

        .data
cursor:     .word 0
curchar:    .word 0
errflag:    .word 0
errMsg:     .asciiz "error"
nanMsg:     .asciiz "nan"
infMsg:     .asciiz "inf"
arrow:      .asciiz " => "
case0:      .asciiz "2+3"
case1:      .asciiz "10/4"
case2:      .asciiz "(3+4)*2-10/4"
case3:      .asciiz "-5+3"
case4:      .asciiz "3*-2"
case5:      .asciiz "2 + 3 * 4"
case6:      .asciiz ".5*2"
case7:      .asciiz "(2+3"
            .align 2
cases:      .word case0, case1, case2, case3, case4, case5, case6, case7, 0

        .text
        .globl main
main:
        addiu $sp, $sp, -16
        sw   $ra, 12($sp)            # -> -4($fp)
        sw   $fp, 8($sp)             # -> -8($fp)
        addiu $fp, $sp, 16
        la   $t0, cases
        sw   $t0, -12($fp)           # casep = &cases[0]
mt_loop:
        lw   $t0, -12($fp)           # casep
        lw   $t1, 0($t0)             # current case string
        sw   $t1, -16($fp)           # cur_case
        beqz $t1, mt_done
        li   $v0, 4                  # print the expression
        move $a0, $t1
        syscall
        li   $v0, 4                  # print " => "
        la   $a0, arrow
        syscall
        lw   $t0, -16($fp)           # eval(this string): cursor = string
        sw   $t0, cursor
        sw   $zero, errflag
        jal  advance                 # prime the first lookahead
        jal  parse_expr              # value -> $f0 (preserved across skip_blanks)
        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, -1
        beq  $t0, $t1, mt_ok
        li   $t1, 1                  # trailing garbage -> error
        sw   $t1, errflag
mt_ok:
        lw   $t0, errflag
        beqz $t0, mt_print
        li   $v0, 4                  # print "error"
        la   $a0, errMsg
        syscall
        j    mt_eol
mt_print:
        mov.d $f12, $f0
        jal  print_double
mt_eol:
        li   $v0, 11                 # newline
        li   $a0, 10
        syscall
        lw   $t0, -12($fp)           # casep++ (next case)
        addi $t0, $t0, 4
        sw   $t0, -12($fp)
        j    mt_loop
mt_done:
        li   $v0, 0
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 16
        jr   $ra


# ======== eval core — calc-eval.asm's parse routines, verbatim ========

# ---------- advance() — cur = next byte of the buffer (-1 at the NUL) ----------
# Leaf helper (no jal): walks the `cursor` global.
advance:
        lw   $t0, cursor
        lbu  $t1, 0($t0)
        beqz $t1, adv_eof
        sw   $t1, curchar
        addi $t0, $t0, 1
        sw   $t0, cursor
        jr   $ra
adv_eof:
        li   $t1, -1
        sw   $t1, curchar
        jr   $ra


# ---------- skip_blanks() — skip ' ' '\t' '\r' ----------
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

        li   $t1, '-'
        bne  $t0, $t1, pf_not_neg
        jal  advance
        jal  parse_factor
        neg.d $f0, $f0
        j    pf_ret
pf_not_neg:
        li   $t1, '('
        bne  $t0, $t1, pf_not_paren
        jal  advance
        jal  parse_expr
        sdc1 $f0, -16($fp)
        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, ')'
        bne  $t0, $t1, pf_paren_err
        jal  advance
        ldc1 $f0, -16($fp)
        j    pf_ret
pf_paren_err:
        li   $t0, 1
        sw   $t0, errflag
        ldc1 $f0, -16($fp)
        j    pf_ret
pf_not_paren:
        li   $t1, '.'
        beq  $t0, $t1, pf_number
        li   $t1, '0'
        blt  $t0, $t1, pf_bad
        li   $t1, '9'
        bgt  $t0, $t1, pf_bad
pf_number:
        mtc1 $zero, $f0
        cvt.d.w $f0, $f0
        li   $t1, 10
        mtc1 $t1, $f4
        cvt.d.w $f4, $f4
        li   $t2, 0                  # fractional_digits (transient)
        li   $t3, 0                  # seen_digit (transient)
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
        bnez $t3, pfn_div
        li   $t0, 1
        sw   $t0, errflag
        mtc1 $zero, $f0
        cvt.d.w $f0, $f0
        j    pf_ret
pfn_div:
        blez $t2, pf_ret
        div.d $f0, $f0, $f4
        addi $t2, $t2, -1
        j    pfn_div
pf_bad:
        li   $t0, 1
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
        sdc1 $f0, -16($fp)
        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, '*'
        beq  $t0, $t1, pt_mul
        li   $t1, '/'
        beq  $t0, $t1, pt_div
        ldc1 $f0, -16($fp)
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 16
        jr   $ra
pt_mul:
        jal  advance
        jal  parse_factor
        ldc1 $f2, -16($fp)
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
print_double:
        c.eq.d $f12, $f12
        bc1t pd_not_nan
        li   $v0, 4
        la   $a0, nanMsg
        syscall
        jr   $ra
pd_not_nan:
        mtc1 $zero, $f14
        cvt.d.w $f14, $f14
        c.lt.d $f12, $f14
        bc1f pd_nonneg
        li   $v0, 11
        li   $a0, '-'
        syscall
        neg.d $f12, $f12
pd_nonneg:
        add.d $f16, $f12, $f12
        c.eq.d $f16, $f12
        bc1f pd_finite
        c.eq.d $f12, $f14
        bc1t pd_finite
        li   $v0, 4
        la   $a0, infMsg
        syscall
        jr   $ra
pd_finite:
        li   $t1, 10
        mtc1 $t1, $f18
        cvt.d.w $f18, $f18
        trunc.w.d $f4, $f12
        mfc1 $t0, $f4
        move $a0, $t0
        li   $v0, 1
        syscall
        cvt.d.w $f6, $f4
        sub.d $f12, $f12, $f6
        c.eq.d $f12, $f14
        bc1t pd_done
        li   $v0, 11
        li   $a0, '.'
        syscall
        li   $t2, 6
pd_frac_loop:
        blez $t2, pd_done
        mul.d $f12, $f12, $f18
        trunc.w.d $f4, $f12
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
