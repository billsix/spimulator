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


# C source — see rpn.c


# Invocations (whitespace-separated RPN tokens on stdin):
#   echo '3 4 + 2 *' | spimulator -f rpn.asm      ->  14
#   echo '1 3 /'     | spimulator -f rpn.asm      ->  0.333333
#   echo '3 4 -'     | spimulator -f rpn.asm      ->  -1


#PURPOSE:  A floating-point reverse-Polish-notation (dc-flavored)
#          calculator.  Reads tokens from stdin: a number is PUSHED,
#          an operator ('+','-','*','/') POPS two values and PUSHES the
#          result.  At EOF the top of stack is printed.  See rpn.c for
#          the portable C reference; both run the SAME algorithm, so the
#          output matches byte-for-byte.
#
#          This is the first example to use the FPU: numbers are doubles,
#          built from the digits with cvt.d.w / mul.d / add.d and divided
#          into place with div.d.  The OPERAND STACK is the real MIPS $sp
#          stack: push = `addi $sp,-8` + `sdc1`, pop = `ldc1` + `addi $sp,8`
#          (each double is 8 bytes).  Reading is byte-at-a-time via syscall
#          12 (read_char, which returns -1 at EOF like getchar()).
#
#          Formatting a double for display is its own deep topic; this
#          demo sidesteps it with a fixed printer (see print_double): an
#          integral value prints as an integer, otherwise as the integer
#          part, '.', and 6 truncated fractional digits.  nan / inf are
#          named.  '-' is always subtract (no negative literals; negative
#          RESULTS print fine).  Underflow -> message + exit 1; divide by
#          zero -> inf/nan, shown (not trapped).


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     depth              $s2                 (evaluation-stack depth)
#     c (current char)   $s3                 (lookahead from read_char)
#     saved $ra          $s0
#     saved $sp          $s1                 (restored before return)
#     operand stack      the $sp hardware stack (doubles, 8 bytes each)
#     mantissa           $f0
#     ten (10.0)         $f4
#     digit-as-double    $f2
#     fractional_digits  $t2
#   In print_double:
#     value              $f12                (argument)
#     zero (0.0)         $f14
#     ten (10.0)         $f18
#     integer_part/digit $t0  (via $f4/$f6)

        .data
underflowMsg:   .asciiz "stack underflow\n"
overflowMsg:    .asciiz "stack overflow\n"
badMsg:         .asciiz "bad input\n"
emptyMsg:       .asciiz "empty\n"
nanMsg:         .asciiz "nan"
infMsg:         .asciiz "inf"

        .text
        .globl main
main:
        move $s0, $ra                # save return address
        move $s1, $sp                # save original $sp (operand stack base)
        li   $s2, 0                  # depth = 0

        li   $v0, 12                 # read_char
        syscall
        move $s3, $v0                # c = read_char()

read_loop:
        li   $t0, -1
        beq  $s3, $t0, at_eof        # while (c != EOF)

        # --- whitespace: ' '(32) '\n'(10) '\t'(9) '\r'(13) ---
        li   $t0, ' '
        beq  $s3, $t0, skip_ws
        li   $t0, 10
        beq  $s3, $t0, skip_ws
        li   $t0, 9
        beq  $s3, $t0, skip_ws
        li   $t0, 13
        beq  $s3, $t0, skip_ws

        # --- digit? '0'(48)..'9'(57) ---
        li   $t0, '0'
        blt  $s3, $t0, check_op
        li   $t0, '9'
        bgt  $s3, $t0, check_op
        j    parse_number

skip_ws:
        li   $v0, 12
        syscall
        move $s3, $v0
        j    read_loop

check_op:
        li   $t0, '+'
        beq  $s3, $t0, do_op
        li   $t0, '-'
        beq  $s3, $t0, do_op
        li   $t0, '*'
        beq  $s3, $t0, do_op
        li   $t0, '/'
        beq  $s3, $t0, do_op
        # unknown character -> "bad input", exit 1
        li   $v0, 4
        la   $a0, badMsg
        syscall
        li   $a0, 1
        li   $v0, 17
        syscall

# ---- parse a number into $f0, then push it ----
parse_number:
        mtc1 $zero, $f0
        cvt.d.w $f0, $f0             # mantissa = 0.0
        li   $t1, 10
        mtc1 $t1, $f4
        cvt.d.w $f4, $f4             # ten = 10.0
        li   $t2, 0                  # fractional_digits = 0

pn_int:                             # integer part
        li   $t0, '0'
        blt  $s3, $t0, pn_after_int
        li   $t0, '9'
        bgt  $s3, $t0, pn_after_int
        addi $t0, $s3, -48           # digit value
        mtc1 $t0, $f2
        cvt.d.w $f2, $f2
        mul.d $f0, $f0, $f4          # mantissa *= 10
        add.d $f0, $f0, $f2          # mantissa += digit
        li   $v0, 12
        syscall
        move $s3, $v0
        j    pn_int

pn_after_int:
        li   $t0, '.'
        bne  $s3, $t0, pn_scale
        li   $v0, 12                 # consume '.'
        syscall
        move $s3, $v0
pn_frac:                            # fractional digits
        li   $t0, '0'
        blt  $s3, $t0, pn_scale
        li   $t0, '9'
        bgt  $s3, $t0, pn_scale
        addi $t0, $s3, -48
        mtc1 $t0, $f2
        cvt.d.w $f2, $f2
        mul.d $f0, $f0, $f4          # mantissa *= 10
        add.d $f0, $f0, $f2          # mantissa += digit
        addi $t2, $t2, 1             # fractional_digits++
        li   $v0, 12
        syscall
        move $s3, $v0
        j    pn_frac

pn_scale:                           # value = mantissa / 10^fractional_digits
        blez $t2, pn_push
        div.d $f0, $f0, $f4
        addi $t2, $t2, -1
        j    pn_scale

pn_push:
        li   $t0, 64                 # STACK_MAX
        blt  $s2, $t0, pn_do_push
        li   $v0, 4
        la   $a0, overflowMsg
        syscall
        li   $a0, 1
        li   $v0, 17
        syscall
pn_do_push:
        addi $sp, $sp, -8
        sdc1 $f0, 0($sp)
        addi $s2, $s2, 1             # depth++
        j    read_loop               # c already holds the lookahead

# ---- apply a binary operator ----
do_op:
        li   $t0, 2
        blt  $s2, $t0, underflow     # need two operands
        ldc1 $f2, 0($sp)             # right = pop()
        addi $sp, $sp, 8
        ldc1 $f0, 0($sp)             # left  = pop()
        addi $sp, $sp, 8
        addi $s2, $s2, -2
        li   $t0, '+'
        beq  $s3, $t0, op_add
        li   $t0, '-'
        beq  $s3, $t0, op_sub
        li   $t0, '*'
        beq  $s3, $t0, op_mul
        div.d $f0, $f0, $f2          # '/'  (div by zero -> inf/nan, shown)
        j    op_store
op_add:
        add.d $f0, $f0, $f2
        j    op_store
op_sub:
        sub.d $f0, $f0, $f2
        j    op_store
op_mul:
        mul.d $f0, $f0, $f2
op_store:
        addi $sp, $sp, -8
        sdc1 $f0, 0($sp)
        addi $s2, $s2, 1             # depth++
        li   $v0, 12                 # read next char
        syscall
        move $s3, $v0
        j    read_loop

underflow:
        li   $v0, 4
        la   $a0, underflowMsg
        syscall
        li   $a0, 1
        li   $v0, 17
        syscall

# ---- end of input: print the top of stack ----
at_eof:
        li   $t0, 1
        blt  $s2, $t0, empty         # need at least one value
        ldc1 $f12, 0($sp)            # top of stack -> print_double arg
        addi $sp, $sp, 8
        addi $s2, $s2, -1
        jal  print_double
        li   $v0, 11
        li   $a0, 10                 # '\n'
        syscall
        move $sp, $s1                # restore original $sp
        move $ra, $s0
        li   $v0, 0
        jr   $ra

empty:
        li   $v0, 4
        la   $a0, emptyMsg
        syscall
        li   $a0, 1
        li   $v0, 17
        syscall


# ---------- print_double($f12) — print a double per the fixed format ----------
#   nan -> "nan";  sign;  inf -> "inf";  else integer part, and if there is a
#   fraction, '.' + 6 truncated fractional digits.  Leaf routine (only syscalls).
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
