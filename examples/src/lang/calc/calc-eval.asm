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


# C source — see calc-eval.c

# Invocation (one infix expression per line on stdin):
#   echo '(3 + 4) * 2 - 10 / 4' | spimulator -f calc-eval.asm   ->  11.500000

# PURPOSE:  The I/O-separated calculator — same language/output as calc-sdt, but
#           the algorithm is split from the I/O.  main reads one line of stdin
#           into the .data buffer `lineBuf`, then `eval` parses+evaluates that
#           BUFFER (the parser's `advance` walks the buffer; nothing in it calls
#           read_char).  So the parser is a pure function of a string.  Shares
#           calc-sdt's golden.  The parse routines below are calc-sdt's verbatim;
#           only `advance`, `main`, and the `eval` wrapper differ.

#SYMBOL TABLE  (C name -> MIPS location)
#   cursor (buffer read ptr) -> word at `cursor` ; cur (lookahead) -> `curchar`
#   had_error -> `errflag` ; a parsed value -> $f0 ; line buffer -> `lineBuf`

        .data
lineBuf:    .space 256
cursor:     .word 0
curchar:    .word 0
errflag:    .word 0
errMsg:     .asciiz "error"
nanMsg:     .asciiz "nan"
infMsg:     .asciiz "inf"

        .text
        .globl main
main:
        move $s0, $ra                # save return address
        li   $v0, 12                 # c = read_char()  (between-lines lookahead)
        syscall
        move $s3, $v0

mloop:
        li   $t0, -1
        beq  $s3, $t0, mdone         # while (c != EOF)

        # read one line (up to 255 bytes) into lineBuf, NUL-terminated
        la   $t1, lineBuf
        li   $t2, 0                  # n
rl_loop:
        li   $t0, -1
        beq  $s3, $t0, rl_end
        li   $t0, 10                 # '\n' ends the line
        beq  $s3, $t0, rl_end
        li   $t0, 255                # LINE_MAX - 1
        bge  $t2, $t0, rl_consume    # buffer full: keep consuming, don't store
        sb   $s3, 0($t1)
        addi $t1, $t1, 1
        addi $t2, $t2, 1
rl_consume:
        li   $v0, 12
        syscall
        move $s3, $v0
        j    rl_loop
rl_end:
        sb   $zero, 0($t1)           # NUL-terminate
        li   $t0, 10                 # consume the newline, prime next lookahead
        bne  $s3, $t0, rl_no_nl
        li   $v0, 12
        syscall
        move $s3, $v0
rl_no_nl:
        jal  blank_line              # blank line -> no output
        bnez $v0, mloop

        jal  eval                    # value -> $f0, errflag set
        lw   $t0, errflag
        beqz $t0, m_print
        li   $v0, 4                  # print "error"
        la   $a0, errMsg
        syscall
        j    m_eol
m_print:
        mov.d $f12, $f0
        jal  print_double
m_eol:
        li   $v0, 11                 # end the output line
        li   $a0, 10
        syscall
        j    mloop

mdone:
        move $ra, $s0
        li   $v0, 0
        jr   $ra


# ---------- blank_line() -> $v0 (1 if lineBuf is empty or all blanks) ----------
blank_line:
        la   $t0, lineBuf
bl_loop:
        lbu  $t1, 0($t0)
        beqz $t1, bl_blank
        li   $t2, ' '
        beq  $t1, $t2, bl_next
        li   $t2, 9
        beq  $t1, $t2, bl_next
        li   $t2, 13
        beq  $t1, $t2, bl_next
        li   $v0, 0                  # a non-blank character
        jr   $ra
bl_next:
        addi $t0, $t0, 1
        j    bl_loop
bl_blank:
        li   $v0, 1
        jr   $ra


# ---------- eval() -> $f0 : the pure core over lineBuf ----------
#   cursor = lineBuf; errflag = 0; parse_expr; flag trailing garbage.
eval:
        addi $sp, $sp, -8
        sw   $ra, 0($sp)
        la   $t0, lineBuf
        sw   $t0, cursor             # cursor = lineBuf
        sw   $zero, errflag
        jal  advance                 # prime the first lookahead char
        jal  parse_expr              # value -> $f0 (preserved across skip_blanks)
        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, -1
        beq  $t0, $t1, eval_ret
        li   $t1, 1                  # trailing garbage after a full expression
        sw   $t1, errflag
eval_ret:
        lw   $ra, 0($sp)
        addi $sp, $sp, 8
        jr   $ra


# ---------- advance() — cur = next byte of the buffer (-1 at the NUL) ----------
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


# ======== the following parse routines are calc-sdt.asm's, verbatim ========

# ---------- skip_blanks() — skip ' ' '\t' '\r' (NOT '\n') ----------
skip_blanks:
        addi $sp, $sp, -4
        sw   $ra, 0($sp)
sb_loop:
        lw   $t0, curchar
        li   $t1, ' '
        beq  $t0, $t1, sb_adv
        li   $t1, 9
        beq  $t0, $t1, sb_adv
        li   $t1, 13
        beq  $t0, $t1, sb_adv
        lw   $ra, 0($sp)
        addi $sp, $sp, 4
        jr   $ra
sb_adv:
        jal  advance
        j    sb_loop


# ---------- parse_factor() -> $f0 ----------
#   factor := NUMBER | '-' factor | '(' expr ')'
parse_factor:
        addi $sp, $sp, -16
        sw   $ra, 8($sp)
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
        sdc1 $f0, 0($sp)             # hold the value across the ')' handling
        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, ')'
        bne  $t0, $t1, pf_paren_err
        jal  advance
        ldc1 $f0, 0($sp)
        j    pf_ret
pf_paren_err:
        li   $t0, 1                  # unbalanced parenthesis
        sw   $t0, errflag
        ldc1 $f0, 0($sp)
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
        li   $t2, 0                  # fractional_digits
        li   $t3, 0                  # seen_digit
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
        lw   $ra, 8($sp)
        addi $sp, $sp, 16
        jr   $ra


# ---------- parse_term() -> $f0 ----------
#   term := factor (('*'|'/') factor)*
parse_term:
        addi $sp, $sp, -16
        sw   $ra, 8($sp)
        jal  parse_factor
pt_loop:
        sdc1 $f0, 0($sp)             # save accumulator across the next call
        jal  skip_blanks
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


# ---------- parse_expr() -> $f0 ----------
#   expr := term (('+'|'-') term)*
parse_expr:
        addi $sp, $sp, -16
        sw   $ra, 8($sp)
        jal  parse_term
pe_loop:
        sdc1 $f0, 0($sp)
        jal  skip_blanks
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


# ---------- print_double($f12) — fixed deterministic format (see rpn.asm) ----
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
