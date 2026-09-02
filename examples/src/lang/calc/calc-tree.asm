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


# C source — see calc-tree.c


# Invocations (one infix expression per line on stdin):
#   echo '(3 + 4) * 2 - 10 / 4' | spimulator -f calc-tree.asm   ->  11.500000
#   echo '2 + 3 * 4'           | spimulator -f calc-tree.asm   ->  14
#   echo '-5 + 3'              | spimulator -f calc-tree.asm   ->  -2


#PURPOSE:  The tree-building companion to calc-sdt: the SAME infix calculator
#          and grammar, but the parser BUILDS AN AST and a SEPARATE recursive
#          walker (eval) evaluates it.  Output is byte-identical to calc-sdt —
#          they share one golden.  See calc-tree.c for the reference.
#
#          Grammar (precedence baked into the rule layering):
#            expr   := term   (('+'|'-') term)*
#            term   := factor (('*'|'/') factor)*
#            factor := NUMBER | '-' factor | '(' expr ')'
#
#          The parser returns a NODE POINTER (in $v0) up the call chain instead
#          of calc-sdt's double; nothing is computed until eval walks the tree.
#          Left operands held across a recursive call are node pointers saved on
#          the $sp frame (a word), where calc-sdt saved doubles.
#
#          Nodes are bump-allocated off the program break with syscall 9 (sbrk):
#          new_node grabs 24 bytes and returns the region base in $v0.  sbrk's
#          base is 8-aligned and 24 keeps the value field (offset 8) 8-aligned,
#          so sdc1/ldc1 to it work with no manual alignment.  Nodes are never
#          freed; a malformed line's partial tree is simply leaked.


#SYMBOL TABLE  (C name -> MIPS location)
#
#   cur (lookahead char)  ->  word at `curchar` (.data)
#   had_error             ->  word at `errflag`  (.data)
#   a Node*               ->  $v0 (returned up the parse call chain)
#   the finished tree      ->  $s2 (held in main across skip_blanks/advance)
#   a parse accumulator (left Node*) held across a call -> 0($sp) frame (word)
#   eval's left operand held across eval(right) -> 0($sp) frame (double)
#
#   Node layout (24 bytes, from new_node):
#     0  kind  (word: 0=NUM, 1=BINOP, 2=NEG)
#     4  op    (word: operator char, BINOP only)
#     8  value (double, NUM only)
#     16 left  (word Node*: BINOP left / NEG child)
#     20 right (word Node*: BINOP right)

        .data
curchar:    .word 0
errflag:    .word 0
errMsg:     .asciiz "error"
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
        jal  parse_expr              # tree -> $v0
        move $s2, $v0                # hold the tree across skip_blanks/advance
                                     # (syscall 12 in advance would clobber $v0)
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
        move $a0, $s2                # eval(tree)
        jal  eval
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
        move $ra, $s0
        li   $v0, 0
        jr   $ra


# ---------- advance() — cur = read_char() ----------
advance:
        li   $v0, 12
        syscall
        sw   $v0, curchar
        jr   $ra


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


# ---------- new_node() -> $v0 — bump-allocate a 24-byte node via sbrk ----------
#   syscall 9 grows the data segment by $a0 and returns the OLD top in $v0.
#   Leaf (only a syscall); the returned base is 8-aligned.
new_node:
        li   $a0, 24
        li   $v0, 9                  # 9 = sbrk
        syscall
        jr   $ra


# ---------- parse_factor() -> $v0 (Node*) ----------
#   factor := NUMBER | '-' factor | '(' expr ')'
#   frame: 0($sp)=held Node* (child/subtree across a call), 4($sp)=$ra
parse_factor:
        addi $sp, $sp, -8
        sw   $ra, 4($sp)
        jal  skip_blanks
        lw   $t0, curchar

        li   $t1, '-'                # unary minus -> NEG node
        bne  $t0, $t1, pf_not_neg
        jal  advance
        jal  parse_factor            # child -> $v0
        sw   $v0, 0($sp)             # hold child across new_node
        jal  new_node                # node -> $v0
        li   $t0, 2
        sw   $t0, 0($v0)             # kind = NEG
        lw   $t1, 0($sp)
        sw   $t1, 16($v0)            # left = child
        j    pf_ret
pf_not_neg:
        li   $t1, '('                # parenthesized expr
        bne  $t0, $t1, pf_not_paren
        jal  advance
        jal  parse_expr              # subtree -> $v0
        sw   $v0, 0($sp)             # hold subtree across the ')' handling
        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, ')'
        bne  $t0, $t1, pf_paren_err
        jal  advance
        lw   $v0, 0($sp)
        j    pf_ret
pf_paren_err:
        li   $t0, 1                  # unbalanced parenthesis
        sw   $t0, errflag
        lw   $v0, 0($sp)
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
        j    pfn_build
pfn_div:
        blez $t2, pfn_build          # value = mantissa / 10^fractional_digits
        div.d $f0, $f0, $f4
        addi $t2, $t2, -1
        j    pfn_div
pf_bad:
        li   $t0, 1                  # unexpected char where a factor was wanted
        sw   $t0, errflag
        mtc1 $zero, $f0
        cvt.d.w $f0, $f0
pfn_build:
        jal  new_node                # NUM node -> $v0 ($f0 survives sbrk)
        sw   $zero, 0($v0)           # kind = NUM (0)
        sdc1 $f0, 8($v0)             # value = $f0
pf_ret:
        lw   $ra, 4($sp)
        addi $sp, $sp, 8
        jr   $ra


# ---------- parse_term() -> $v0 (Node*) ----------
#   term := factor (('*'|'/') factor)*  — left-associative BINOP nesting
#   frame: 0($sp)=left Node*, 4($sp)=right Node*, 8($sp)=$ra
parse_term:
        addi $sp, $sp, -16
        sw   $ra, 8($sp)
        jal  parse_factor            # left -> $v0
pt_loop:
        sw   $v0, 0($sp)             # save left across the next call
        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, '*'
        beq  $t0, $t1, pt_mul
        li   $t1, '/'
        beq  $t0, $t1, pt_div
        lw   $v0, 0($sp)             # neither: restore and return left
        lw   $ra, 8($sp)
        addi $sp, $sp, 16
        jr   $ra
pt_mul:
        jal  advance
        jal  parse_factor            # right -> $v0
        li   $t4, '*'
        j    pt_build
pt_div:
        jal  advance
        jal  parse_factor
        li   $t4, '/'
pt_build:
        sw   $v0, 4($sp)             # save right; $t4 = op (safe: no calls until
        jal  new_node                # it is stored below)
        li   $t0, 1
        sw   $t0, 0($v0)             # kind = BINOP
        sw   $t4, 4($v0)             # op
        lw   $t1, 0($sp)
        sw   $t1, 16($v0)            # left
        lw   $t1, 4($sp)
        sw   $t1, 20($v0)            # right
        j    pt_loop                 # new node becomes the left operand


# ---------- parse_expr() -> $v0 (Node*) ----------
#   expr := term (('+'|'-') term)*  — left-associative BINOP nesting
#   frame: 0($sp)=left Node*, 4($sp)=right Node*, 8($sp)=$ra
parse_expr:
        addi $sp, $sp, -16
        sw   $ra, 8($sp)
        jal  parse_term              # left -> $v0
pe_loop:
        sw   $v0, 0($sp)
        jal  skip_blanks
        lw   $t0, curchar
        li   $t1, '+'
        beq  $t0, $t1, pe_add
        li   $t1, '-'
        beq  $t0, $t1, pe_sub
        lw   $v0, 0($sp)
        lw   $ra, 8($sp)
        addi $sp, $sp, 16
        jr   $ra
pe_add:
        jal  advance
        jal  parse_term
        li   $t4, '+'
        j    pe_build
pe_sub:
        jal  advance
        jal  parse_term
        li   $t4, '-'
pe_build:
        sw   $v0, 4($sp)
        jal  new_node
        li   $t0, 1
        sw   $t0, 0($v0)             # kind = BINOP
        sw   $t4, 4($v0)             # op
        lw   $t1, 0($sp)
        sw   $t1, 16($v0)            # left
        lw   $t1, 4($sp)
        sw   $t1, 20($v0)            # right
        j    pe_loop


# ---------- eval($a0 = Node*) -> $f0 — the recursive tree walker ----------
#   NUM   -> value ; NEG -> -eval(left) ; BINOP -> apply op to eval(l), eval(r)
#   frame: 0($sp)=left operand (double, held across eval(right)),
#          8($sp)=node ptr, 12($sp)=$ra
eval:
        addi $sp, $sp, -16
        sw   $ra, 12($sp)
        sw   $a0, 8($sp)             # save node ptr
        lw   $t0, 0($a0)             # kind
        beqz $t0, ev_num             # 0 = NUM
        li   $t1, 2
        beq  $t0, $t1, ev_neg        # 2 = NEG
        # else BINOP (1)
        lw   $a0, 16($a0)            # left child
        jal  eval                    # $f0 = eval(left)
        sdc1 $f0, 0($sp)             # hold left value across eval(right)
        lw   $t0, 8($sp)
        lw   $a0, 20($t0)            # right child
        jal  eval                    # $f0 = eval(right)
        ldc1 $f2, 0($sp)             # $f2 = left value, $f0 = right value
        lw   $t0, 8($sp)
        lw   $t1, 4($t0)             # op
        li   $t2, '+'
        beq  $t1, $t2, ev_add
        li   $t2, '-'
        beq  $t1, $t2, ev_sub
        li   $t2, '*'
        beq  $t1, $t2, ev_mul
        div.d $f0, $f2, $f0          # '/'
        j    ev_ret
ev_add:
        add.d $f0, $f2, $f0
        j    ev_ret
ev_sub:
        sub.d $f0, $f2, $f0
        j    ev_ret
ev_mul:
        mul.d $f0, $f2, $f0
        j    ev_ret
ev_num:
        ldc1 $f0, 8($a0)             # value ($a0 still the node ptr)
        j    ev_ret
ev_neg:
        lw   $a0, 16($a0)            # child
        jal  eval
        neg.d $f0, $f0
ev_ret:
        lw   $ra, 12($sp)
        addi $sp, $sp, 16
        jr   $ra


# ---------- print_double($f12) — fixed deterministic format (see rpn.asm) ----
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
