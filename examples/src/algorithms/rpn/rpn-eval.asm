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


# C source — see rpn-eval.c

# Invocation (whitespace-separated RPN tokens on stdin):
#   echo '3 4 + 2 * 1 3 / -' | spimulator -f rpn-eval.asm      ->  13.666666

# PURPOSE:  The I/O-separated RPN calculator.  Same language and output as
#           rpn.asm, but the ALGORITHM is split from the I/O: `eval` evaluates a
#           NUL-terminated expression BUFFER (tokens come from `advance`, which
#           walks a .data buffer, NOT from syscall 12) and returns the top of the
#           evaluation stack in $f0, recording any error in `status`; `main`
#           slurps stdin into that buffer, calls eval, then prints the value or
#           maps the status to rpn's message + exit code.  Because eval cannot
#           do I/O, an error is reported in `status` instead of printed, so the
#           behavior matches rpn and this shares rpn's golden.
#
#           `status` codes (mirror enum eval_status in rpn-eval.c):
#             0 = OK   1 = overflow   2 = underflow   3 = bad input   4 = empty
#
#           The operand stack is the .data array `opstack` indexed by `depth`
#           (eval's `double stack[STACK_MAX]` local; a non-recursive routine's
#           array behaves like static storage, which keeps the asm legible).
#           This differs from rpn.asm's operand-stack-is-$sp lesson on purpose —
#           here the lesson is the algorithm/I/O split, and eval must be an
#           ordinary function with a normal frame for its saved $ra.
#
#           STACK-VISIBLE STYLE: this demo has no persistent scalar C locals on
#           a frame — its state lives in .data GLOBALS (the operand stack
#           `opstack`, the `depth`, the `status`, the read `cursor`/`curchar`).
#           main and eval therefore just carry the standard $ra/$fp frame for
#           their jals; main's slurp write-pointer and eval's fractional_digits
#           are transient scratch registers.  advance is a leaf buffer walker;
#           print_double is a shared display helper.
#           Style reference: tasks/reference/mips-asm-teaching-style.md
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  stdin: algorithms/rpn/rpn.input
#  stdout:
#  | 13.666666

        .data
line:           .space 256          # stdin is slurped here, then NUL-terminated
opstack:        .space 512          # evaluation stack: 64 doubles (8 bytes each)
cursor:         .word 0             # read position within the buffer
curchar:        .word 0             # lookahead character (-1 at end of buffer)
depth:          .word 0             # evaluation-stack depth
status:         .word 0             # 0 ok / 1 overflow / 2 underflow / 3 bad / 4 empty
underflowMsg:   .asciiz "stack underflow\n"
overflowMsg:    .asciiz "stack overflow\n"
badMsg:         .asciiz "bad input\n"
emptyMsg:       .asciiz "empty\n"
nanMsg:         .asciiz "nan"
infMsg:         .asciiz "inf"

        .text
        .globl main
main:
        addiu $sp, $sp, -8
        sw   $ra, 4($sp)             # -> -4($fp)
        sw   $fp, 0($sp)             # -> -8($fp)
        addiu $fp, $sp, 8

        # --- slurp all of stdin into `line`, then NUL-terminate ---
        la   $t1, line               # write pointer (transient)
        la   $t2, line
        addi $t2, $t2, 255           # cap (reserve the last byte for the NUL)
rd_loop:
        li   $v0, 12                 # read_char
        syscall
        li   $t0, -1
        beq  $v0, $t0, rd_done       # EOF
        bge  $t1, $t2, rd_loop       # buffer full: drop the byte, keep draining
        sb   $v0, 0($t1)
        addi $t1, $t1, 1
        j    rd_loop
rd_done:
        sb   $zero, 0($t1)           # NUL terminator

        la   $a0, line               # eval(line)
        jal  eval                    # value -> $f0, status set

        lw   $t0, status
        beqz $t0, m_value            # 0 = OK
        li   $t1, 1
        beq  $t0, $t1, m_overflow
        li   $t1, 2
        beq  $t0, $t1, m_underflow
        li   $t1, 3
        beq  $t0, $t1, m_bad
        la   $a0, emptyMsg           # else 4 = empty
        j    m_error
m_overflow:
        la   $a0, overflowMsg
        j    m_error
m_underflow:
        la   $a0, underflowMsg
        j    m_error
m_bad:
        la   $a0, badMsg
m_error:
        li   $v0, 4
        syscall
        li   $a0, 1                  # exit code 1
        li   $v0, 17
        syscall
m_value:
        mov.d $f12, $f0
        jal  print_double
        li   $v0, 11
        li   $a0, 10                 # '\n'
        syscall
        li   $v0, 0                  # return 0
        lw   $ra, -4($fp)
        lw   $fp, -8($fp)
        addiu $sp, $sp, 8
        jr   $ra


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

        # --- whitespace: ' '(32) '\n'(10) '\t'(9) '\r'(13) ---
        li   $t1, ' '
        beq  $t0, $t1, ev_ws
        li   $t1, 10
        beq  $t0, $t1, ev_ws
        li   $t1, 9
        beq  $t0, $t1, ev_ws
        li   $t1, 13
        beq  $t0, $t1, ev_ws

        # --- digit? '0'(48)..'9'(57) ---
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

# ---- parse a number into $f0, then push it ----
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
        mul.d $f0, $f0, $f4          # mantissa *= 10
        add.d $f0, $f0, $f2          # mantissa += digit
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
        mul.d $f0, $f0, $f4          # mantissa *= 10
        add.d $f0, $f0, $f2          # mantissa += digit
        addi $t2, $t2, 1             # fractional_digits++
        jal  advance
        j    pn_frac
pn_scale:                           # value = mantissa / 10^fractional_digits
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
        sll  $t1, $t3, 3             # opstack[depth] = value
        la   $t4, opstack
        add  $t4, $t4, $t1
        sdc1 $f0, 0($t4)
        addi $t3, $t3, 1
        sw   $t3, depth
        j    ev_loop                 # curchar already holds the lookahead

# ---- apply a binary operator ----
ev_do_op:
        lw   $t3, depth
        li   $t1, 2
        blt  $t3, $t1, ev_underflow  # need two operands
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
        addi $t3, $t3, 1             # depth++
        sw   $t3, depth
        jal  advance                 # read next char
        j    ev_loop

ev_underflow:
        li   $t1, 2
        sw   $t1, status
        j    ev_ret

# ---- end of input: return the top of stack ----
ev_eof:
        lw   $t3, depth
        li   $t1, 1
        blt  $t3, $t1, ev_empty      # need at least one value
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


# ---------- print_double($f12) — print a double per the fixed format ----------
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
        neg.d $f12, $f12             # value = -value
pd_nonneg:
        add.d $f16, $f12, $f12       # inf check: value != 0 && value+value == value
        c.eq.d $f16, $f12
        bc1f pd_finite
        c.eq.d $f12, $f14            # value == 0 ?
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
        c.eq.d $f12, $f14            # fraction == 0 ?
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
        addi $a0, $t0, 48            # '0' + digit
        li   $v0, 11
        syscall
        cvt.d.w $f6, $f4             # (double)digit
        sub.d $f12, $f12, $f6        # fraction -= digit
        addi $t2, $t2, -1
        j    pd_frac_loop
pd_done:
        jr   $ra
