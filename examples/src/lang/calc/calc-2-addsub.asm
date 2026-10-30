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


# C source — see calc-2-addsub.c

# Invocation (one expression per line on stdin):
#   printf '2+3-1\n' | spimulator -f calc-2-addsub.asm   ->  4

# PURPOSE:  Rung 2 of the calculator ladder — integer '+'/'-', left to right.
#           expr := DIGITS (('+'|'-') DIGITS)*.  The accumulator is held on the
#           stack across each parse_number() call (0($sp) in parse_expr).
#           Integers only; no spaces; no error path (see calc-sdt for those).

# SYMBOL TABLE  (C name -> MIPS location)
#   cur (lookahead char)       -> word at `curchar` (.data)
#   parse_expr's running value -> 0($sp) in parse_expr's frame
#   parse_number's value -> $t2 ; a digit -> $t0

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


# ---------- parse_expr() -> $v0   expr := DIGITS (('+'|'-') DIGITS)* ----------
#   frame: 0($sp)=accumulator, 4($sp)=$ra
parse_expr:
        addi $sp, $sp, -8
        sw   $ra, 4($sp)
        jal  parse_number            # first operand -> $v0
        sw   $v0, 0($sp)             # accumulator = first operand
pe_loop:
        lw   $t0, curchar
        li   $t1, '+'
        beq  $t0, $t1, pe_add
        li   $t1, '-'
        beq  $t0, $t1, pe_sub
        lw   $v0, 0($sp)             # neither: return the accumulator
        lw   $ra, 4($sp)
        addi $sp, $sp, 8
        jr   $ra
pe_add:
        jal  advance
        jal  parse_number            # rhs -> $v0
        lw   $t2, 0($sp)             # acc = acc + rhs
        add  $t2, $t2, $v0
        sw   $t2, 0($sp)
        j    pe_loop
pe_sub:
        jal  advance
        jal  parse_number
        lw   $t2, 0($sp)             # acc = acc - rhs
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
        addi $t0, $t0, -48           # digit = cur - '0'
        mul  $t2, $t2, $t3           # value = value * 10 + digit
        add  $t2, $t2, $t0
        jal  advance
        j    pn_loop
pn_done:
        move $v0, $t2
        lw   $ra, 0($sp)
        addi $sp, $sp, 4
        jr   $ra
