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


# C source — see rpn-eval-test.c

# Invocation (no input; runs the test battery):
#   spimulator -f rpn-eval-test.asm   ->  "<expr> => <result>" per case

# PURPOSE:  Unit tests for the I/O-separated RPN core.  Because the evaluator
#           walks a .data CHARACTER BUFFER (advance reads `cursor`, not stdin), it
#           can be driven by a table of hardcoded RPN strings with no I/O.  main
#           loops the `cases` table, printing "<expr> => <result>" for each — a
#           value, or the error kind (status) eval reported; run-demo.sh pins the
#           golden, so an evaluator regression turns it red.  The eval core
#           (advance/eval/print_double) is rpn-eval.asm's, verbatim.
#
#           `status` codes (mirror enum eval_status in rpn-eval-test.c):
#             0 = OK   1 = overflow   2 = underflow   3 = bad input   4 = empty
#
#           STACK-VISIBLE STYLE: main homes its two C locals — the case-table
#           pointer and the current expression string — at -12($fp) and -16($fp);
#           a scratch register holds only a working COPY.  The eval core
#           (eval/advance/print_double) is rpn-eval.asm's verbatim: its state is
#           in .data GLOBALS (opstack/depth/status/cursor), so eval just carries
#           the standard $ra/$fp frame.  main saves $ra for its jals.
#           Style reference: tasks/reference/mips-asm-teaching-style.md
#
#           No #CANONICAL block: the empty-expression case prints the line
#           " => empty", whose leading space a comment-based anchor can't carry.
#           Golden-tested via run-demo.sh.

        .data
opstack:    .space 512               # evaluation stack: 64 doubles (8 bytes each)
cursor:     .word 0
curchar:    .word 0
depth:      .word 0
status:     .word 0
nanMsg:     .asciiz "nan"
infMsg:     .asciiz "inf"
arrow:      .asciiz " => "
# Error labels (no newline — the driver appends it), matching rpn's messages:
underflowMsg: .asciiz "stack underflow"
overflowMsg:  .asciiz "stack overflow"
badMsg:       .asciiz "bad input"
emptyMsg:     .asciiz "empty"
case0:      .asciiz "3 4 +"
case1:      .asciiz "3 4 + 2 *"
case2:      .asciiz "1 3 /"
case3:      .asciiz "3 4 -"
case4:      .asciiz "3 4 + 2 * 1 3 / -"
case5:      .asciiz "5"
case6:      .asciiz "1 2 3"
case7:      .asciiz "+"
case8:      .asciiz ""
case9:      .asciiz "1 2 x"
            .align 2
cases:      .word case0, case1, case2, case3, case4, case5, case6, case7, case8, case9, 0

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
        move $a0, $t1                # eval(this string): value -> $f0, status set
        jal  eval
        li   $v0, 4                  # print the expression
        lw   $a0, -16($fp)
        syscall
        li   $v0, 4                  # print " => "
        la   $a0, arrow
        syscall
        lw   $t0, status
        beqz $t0, mt_value           # 0 = OK
        li   $t1, 1
        beq  $t0, $t1, mt_overflow
        li   $t1, 2
        beq  $t0, $t1, mt_underflow
        li   $t1, 3
        beq  $t0, $t1, mt_bad
        la   $a0, emptyMsg           # else 4 = empty
        j    mt_label
mt_overflow:
        la   $a0, overflowMsg
        j    mt_label
mt_underflow:
        la   $a0, underflowMsg
        j    mt_label
mt_bad:
        la   $a0, badMsg
mt_label:
        li   $v0, 4
        syscall
        j    mt_eol
mt_value:
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


# ======== eval core — rpn-eval.asm's routines, verbatim ========

# ---------- eval($a0 = expr buffer) -> $f0  (sets `status`) ----------
eval:
        addiu $sp, $sp, -8
        sw   $ra, 4($sp)             # -> -4($fp)
        sw   $fp, 0($sp)             # -> -8($fp)
        addiu $fp, $sp, 8
        sw   $a0, cursor             # cursor = expr
        sw   $zero, status           # status = OK
        sw   $zero, depth            # depth = 0
        jal  advance                 # prime the first lookahead

ev_loop:
        lw   $t0, curchar
        li   $t1, -1
        beq  $t0, $t1, ev_eof        # while (cur != -1)
        li   $t1, ' '
        beq  $t0, $t1, ev_ws
        li   $t1, 10
        beq  $t0, $t1, ev_ws
        li   $t1, 9
        beq  $t0, $t1, ev_ws
        li   $t1, 13
        beq  $t0, $t1, ev_ws
        li   $t1, '0'
        blt  $t0, $t1, ev_op
        li   $t1, '9'
        bgt  $t0, $t1, ev_op
        j    ev_number
ev_ws:
        jal  advance
        j    ev_loop
ev_op:
        li   $t1, '+'
        beq  $t0, $t1, ev_do_op
        li   $t1, '-'
        beq  $t0, $t1, ev_do_op
        li   $t1, '*'
        beq  $t0, $t1, ev_do_op
        li   $t1, '/'
        beq  $t0, $t1, ev_do_op
        li   $t1, 3                  # unknown character -> bad input
        sw   $t1, status
        j    ev_ret

ev_number:
        mtc1 $zero, $f0
        cvt.d.w $f0, $f0             # mantissa = 0.0
        li   $t1, 10
        mtc1 $t1, $f4
        cvt.d.w $f4, $f4             # ten = 10.0
        li   $t2, 0                  # fractional_digits = 0
pn_int:
        lw   $t0, curchar
        li   $t1, '0'
        blt  $t0, $t1, pn_after_int
        li   $t1, '9'
        bgt  $t0, $t1, pn_after_int
        addi $t0, $t0, -48
        mtc1 $t0, $f2
        cvt.d.w $f2, $f2
        mul.d $f0, $f0, $f4
        add.d $f0, $f0, $f2
        jal  advance
        j    pn_int
pn_after_int:
        lw   $t0, curchar
        li   $t1, '.'
        bne  $t0, $t1, pn_scale
        jal  advance                 # consume '.'
pn_frac:
        lw   $t0, curchar
        li   $t1, '0'
        blt  $t0, $t1, pn_scale
        li   $t1, '9'
        bgt  $t0, $t1, pn_scale
        addi $t0, $t0, -48
        mtc1 $t0, $f2
        cvt.d.w $f2, $f2
        mul.d $f0, $f0, $f4
        add.d $f0, $f0, $f2
        addi $t2, $t2, 1
        jal  advance
        j    pn_frac
pn_scale:
        blez $t2, pn_push
        div.d $f0, $f0, $f4
        addi $t2, $t2, -1
        j    pn_scale
pn_push:
        lw   $t3, depth
        li   $t1, 64                 # STACK_MAX
        blt  $t3, $t1, pn_store
        li   $t1, 1                  # overflow
        sw   $t1, status
        j    ev_ret
pn_store:
        sll  $t1, $t3, 3
        la   $t4, opstack
        add  $t4, $t4, $t1
        sdc1 $f0, 0($t4)
        addi $t3, $t3, 1
        sw   $t3, depth
        j    ev_loop

ev_do_op:
        lw   $t3, depth
        li   $t1, 2
        blt  $t3, $t1, ev_underflow
        addi $t3, $t3, -1            # right = pop()
        sll  $t1, $t3, 3
        la   $t4, opstack
        add  $t4, $t4, $t1
        ldc1 $f2, 0($t4)
        addi $t3, $t3, -1            # left = pop()
        sll  $t1, $t3, 3
        la   $t4, opstack
        add  $t4, $t4, $t1
        ldc1 $f0, 0($t4)
        sw   $t3, depth              # depth -= 2
        lw   $t0, curchar
        li   $t1, '+'
        beq  $t0, $t1, ev_add
        li   $t1, '-'
        beq  $t0, $t1, ev_sub
        li   $t1, '*'
        beq  $t0, $t1, ev_mul
        div.d $f0, $f0, $f2          # '/' (div by zero -> inf/nan, shown)
        j    ev_store
ev_add:
        add.d $f0, $f0, $f2
        j    ev_store
ev_sub:
        sub.d $f0, $f0, $f2
        j    ev_store
ev_mul:
        mul.d $f0, $f0, $f2
ev_store:
        lw   $t3, depth
        sll  $t1, $t3, 3
        la   $t4, opstack
        add  $t4, $t4, $t1
        sdc1 $f0, 0($t4)
        addi $t3, $t3, 1
        sw   $t3, depth
        jal  advance
        j    ev_loop

ev_underflow:
        li   $t1, 2
        sw   $t1, status
        j    ev_ret

ev_eof:
        lw   $t3, depth
        li   $t1, 1
        blt  $t3, $t1, ev_empty
        addi $t3, $t3, -1
        sll  $t1, $t3, 3
        la   $t4, opstack
        add  $t4, $t4, $t1
        ldc1 $f0, 0($t4)             # top of stack -> $f0
        j    ev_ret
ev_empty:
        li   $t1, 4
        sw   $t1, status
ev_ret:
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 8
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


# ---------- print_double($f12) ----------
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
