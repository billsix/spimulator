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


# A port of the design of jonesforth (Richard W.M. Jones, public domain,
# version 47: http://annexia.org/forth, mirrored at
# https://github.com/nornagon/jonesforth) to MIPS and spim's syscalls.

# Invocation (no input; the program is written into the .data segment):
#   spimulator -f forth-3-primitives.asm   ->  1 2 ... 10 / 3 2 1 / ...

# PURPOSE:  Rung 3 of the Forth ladder — the PRIMITIVES, and CONTROL
#           FLOW INSIDE THREADED CODE.  Rung 2 (forth-2-docol.asm) had
#           DOCOL/EXIT and four primitives.  This rung adds jonesforth's
#           machine-code word set: the stack words (DROP SWAP OVER ROT -ROT
#           2DROP 2DUP 2SWAP ?DUP), arithmetic (1+ 1- 4+ 4- - /MOD),
#           comparisons (= <> < > <= >= 0= 0<> 0< 0> 0<= 0>=), bitwise
#           (AND OR XOR INVERT), memory (! @ +! -! C! C@ CMOVE), the
#           stacks as values (>R R> RSP@ RSP! RDROP DSP@ DSP!), and the two
#           branches, BRANCH and 0BRANCH.
#
#           A branch is a codeword followed by a byte OFFSET cell; BRANCH
#           adds the offset to IP.  So a loop is just cells:
#
#             : COUNT-TO-10  1 BEGIN DUP . 1+ DUP 11 = UNTIL DROP CR ;
#
#             cell:  0   1  2   3  4  5   6   7  8  9       10   11   12 13
#                   LIT  1 DUP  . 1+ DUP LIT 11  = 0BRANCH -32  DROP CR EXIT
#                          ^                                 |
#                          +------- (2 - 10) * 4 = -32 ------+
#
#           (rung 8 has Forth compute these offsets; here they are
#           counted by hand).  The program runs COUNT-TO-10, a count-down
#           loop, and one line each of division, comparison, ROT, bitwise,
#           memory and return-stack words.
#
#           New: every primitive above; a .data cell (`counter`) used as a
#           variable.
#
#           STYLE EXEMPTION: the Forth ladder keeps its machine state in
#           fixed registers (below), the way jonesforth does on i386, so it
#           is exempt from the stack-visible style of
#           tasks/reference/mips-asm-teaching-style.md: there are no C
#           locals, no $fp frames and no SYMBOL TABLE.  The register map
#           takes their place.
#
# ------------------------------------------------------------------
#  REGISTER MAP
#
#  register | Forth name | role
#  ---------+------------+--------------------------------------------------
#  $s7      | IP         | instruction pointer: the next cell of the thread
#  $s6      | RSP        | return stack pointer, over return_stack in .data;
#           |            | saved IPs, push = addiu $s6,$s6,-4 + sw
#  $sp      | DSP        | data stack pointer; top of stack at 0($sp),
#           |            | push = addiu $sp,$sp,-4 + sw, pop = lw + addiu 4
#  $t0      | W          | the codeword address NEXT just fetched (DOCOL
#           |            | reads the word's body from W + 4)
#  $t1-$t4  | —          | scratch inside one primitive
#
#  Truth values are jonesforth's 1 (true) and 0 (false), not ANS Forth's
#  -1 and 0.

        .data

# ---- the program -------------------------------------------------------
program:
        .word COUNT_TO_10                       # 1 2 ... 10
        .word LIT, 3, COUNTDOWN                 # 3 2 1
        .word LIT, 17, LIT, 5, DIVMOD, DOT, DOT, CR       # 3 2
        .word LIT, 3, LIT, 4, LT, DOT                     # 1
        .word LIT, 3, LIT, 4, GT, DOT                     # 0
        .word LIT, 0, ZEQU, DOT, CR                       # 1
        .word LIT, 1, LIT, 2, LIT, 3, ROT, DOT, DOT, DOT, CR    # 1 3 2
        .word LIT, 12, LIT, 10, AND, DOT                  # 8
        .word LIT, 12, LIT, 10, OR, DOT                   # 14
        .word LIT, 12, LIT, 10, XOR, DOT, CR              # 6
        .word LIT, 42, LIT, counter, STORE                # counter = 42
        .word LIT, 8, LIT, counter, ADDSTORE              # counter += 8
        .word LIT, counter, FETCH, DOT, CR                # 50
        .word LIT, 5, TOR, LIT, 6, FROMR, SUB, DOT        # 6 - 5 = 1
        .word LIT, 2, LIT, 9, SUB, DOT, CR                # -7
        .word BYE

# ---- words written in Forth --------------------------------------------
# : COUNT-TO-10  1 BEGIN DUP . 1+ DUP 11 = UNTIL DROP CR ;
COUNT_TO_10:
        .word docol
        .word LIT, 1                  # cells 0-1
        .word DUP, DOT, INCR          # cells 2-4   <- loop top
        .word DUP, LIT, 11, EQU       # cells 5-8
        .word ZBRANCH, -32            # cells 9-10: back to cell 2 if not 11
        .word DROP, CR, EXIT          # cells 11-13

# : COUNTDOWN ( n -- )  BEGIN ?DUP WHILE DUP . 1- REPEAT CR ;
COUNTDOWN:
        .word docol
        .word QDUP                    # cell 0      <- loop top
        .word ZBRANCH, 24             # cells 1-2: out to cell 8 when n = 0
        .word DUP, DOT, DECR          # cells 3-5
        .word BRANCH, -28             # cells 6-7: back to cell 0
        .word CR, EXIT                # cells 8-9

# ---- codewords: one cell per primitive, holding its code address -------
EXIT:      .word code_EXIT
LIT:       .word code_LIT
DROP:      .word code_DROP
SWAP:      .word code_SWAP
DUP:       .word code_DUP
OVER:      .word code_OVER
ROT:       .word code_ROT
NROT:      .word code_NROT
TWODROP:   .word code_TWODROP
TWODUP:    .word code_TWODUP
TWOSWAP:   .word code_TWOSWAP
QDUP:      .word code_QDUP
INCR:      .word code_INCR
DECR:      .word code_DECR
INCR4:     .word code_INCR4
DECR4:     .word code_DECR4
ADD:       .word code_ADD
SUB:       .word code_SUB
MUL:       .word code_MUL
DIVMOD:    .word code_DIVMOD
EQU:       .word code_EQU
NEQU:      .word code_NEQU
LT:        .word code_LT
GT:        .word code_GT
LE:        .word code_LE
GE:        .word code_GE
ZEQU:      .word code_ZEQU
ZNEQU:     .word code_ZNEQU
ZLT:       .word code_ZLT
ZGT:       .word code_ZGT
ZLE:       .word code_ZLE
ZGE:       .word code_ZGE
AND:       .word code_AND
OR:        .word code_OR
XOR:       .word code_XOR
INVERT:    .word code_INVERT
STORE:     .word code_STORE
FETCH:     .word code_FETCH
ADDSTORE:  .word code_ADDSTORE
SUBSTORE:  .word code_SUBSTORE
STOREBYTE: .word code_STOREBYTE
FETCHBYTE: .word code_FETCHBYTE
CMOVE:     .word code_CMOVE
TOR:       .word code_TOR
FROMR:     .word code_FROMR
RSPFETCH:  .word code_RSPFETCH
RSPSTORE:  .word code_RSPSTORE
RDROP:     .word code_RDROP
DSPFETCH:  .word code_DSPFETCH
DSPSTORE:  .word code_DSPSTORE
BRANCH:    .word code_BRANCH
ZBRANCH:   .word code_ZBRANCH
DOT:       .word code_DOT
CR:        .word code_CR
BYE:       .word code_BYE

# ---- a variable the program stores into --------------------------------
counter:
        .word 0

# ---- the return stack: 256 saved IPs, growing down from the top --------
return_stack:
        .space 1024
return_stack_top:
        .word 0

        .text
        .globl main
main:
        la    $s6, return_stack_top   # RSP = empty return stack
        la    $s7, program            # IP = the first cell of the program
        # fall into NEXT

# ---- NEXT: the inner interpreter --------------------------------------
next:
        lw    $t0, 0($s7)             # W = *IP
        addiu $s7, $s7, 4             # IP += 4
        lw    $t1, 0($t0)             # the codeword: W's machine code
        jr    $t1

# ---- DOCOL: the codeword of every word written in Forth ---------------
# W ($t0) is the address of the word's codeword cell; its body starts at
# the next cell.
docol:
        addiu $s6, $s6, -4            # push IP on the return stack
        sw    $s7, 0($s6)
        addiu $s7, $t0, 4             # IP = W + 4: the first body cell
        j     next

# ---- EXIT ( -- ): return from a word written in Forth ------------------
code_EXIT:
        lw    $s7, 0($s6)             # pop IP from the return stack
        addiu $s6, $s6, 4
        j     next

# ---- LIT ( -- n ): push the cell after LIT, and skip it ---------------
code_LIT:
        lw    $t1, 0($s7)             # n = *IP
        addiu $s7, $s7, 4             # IP += 4: step over the number
        addiu $sp, $sp, -4            # push n
        sw    $t1, 0($sp)
        j     next

# ======================================================================
#  STACK PRIMITIVES  (stack pictures: before -- after, top on the right)
# ======================================================================

# ---- DROP ( a -- ) -----------------------------------------------------
code_DROP:
        addiu $sp, $sp, 4
        j     next

# ---- SWAP ( a b -- b a ) -----------------------------------------------
code_SWAP:
        lw    $t1, 0($sp)             # b
        lw    $t2, 4($sp)             # a
        sw    $t2, 0($sp)
        sw    $t1, 4($sp)
        j     next

# ---- DUP ( a -- a a ) --------------------------------------------------
code_DUP:
        lw    $t1, 0($sp)
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
        j     next

# ---- OVER ( a b -- a b a ) ---------------------------------------------
code_OVER:
        lw    $t1, 4($sp)             # a
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
        j     next

# ---- ROT ( a b c -- b c a ) --------------------------------------------
code_ROT:
        lw    $t1, 0($sp)             # c
        lw    $t2, 4($sp)             # b
        lw    $t3, 8($sp)             # a
        sw    $t2, 8($sp)
        sw    $t1, 4($sp)
        sw    $t3, 0($sp)
        j     next

# ---- -ROT ( a b c -- c a b ) -------------------------------------------
code_NROT:
        lw    $t1, 0($sp)             # c
        lw    $t2, 4($sp)             # b
        lw    $t3, 8($sp)             # a
        sw    $t1, 8($sp)
        sw    $t3, 4($sp)
        sw    $t2, 0($sp)
        j     next

# ---- 2DROP ( a b -- ) --------------------------------------------------
code_TWODROP:
        addiu $sp, $sp, 8
        j     next

# ---- 2DUP ( a b -- a b a b ) -------------------------------------------
code_TWODUP:
        lw    $t1, 0($sp)             # b
        lw    $t2, 4($sp)             # a
        addiu $sp, $sp, -8
        sw    $t2, 4($sp)
        sw    $t1, 0($sp)
        j     next

# ---- 2SWAP ( a b c d -- c d a b ) --------------------------------------
code_TWOSWAP:
        lw    $t1, 0($sp)             # d
        lw    $t2, 4($sp)             # c
        lw    $t3, 8($sp)             # b
        lw    $t4, 12($sp)            # a
        sw    $t2, 12($sp)
        sw    $t1, 8($sp)
        sw    $t4, 4($sp)
        sw    $t3, 0($sp)
        j     next

# ---- ?DUP ( a -- a a | 0 ): duplicate only if non-zero -----------------
code_QDUP:
        lw    $t1, 0($sp)
        beqz  $t1, qdup_done
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
qdup_done:
        j     next

# ======================================================================
#  ARITHMETIC  (the top of stack is replaced in place)
# ======================================================================

# ---- 1+ ( a -- a+1 ) ---------------------------------------------------
code_INCR:
        lw    $t1, 0($sp)
        addiu $t1, $t1, 1
        sw    $t1, 0($sp)
        j     next

# ---- 1- ( a -- a-1 ) ---------------------------------------------------
code_DECR:
        lw    $t1, 0($sp)
        addiu $t1, $t1, -1
        sw    $t1, 0($sp)
        j     next

# ---- 4+ ( a -- a+4 ): step an address by one cell ----------------------
code_INCR4:
        lw    $t1, 0($sp)
        addiu $t1, $t1, 4
        sw    $t1, 0($sp)
        j     next

# ---- 4- ( a -- a-4 ) ---------------------------------------------------
code_DECR4:
        lw    $t1, 0($sp)
        addiu $t1, $t1, -4
        sw    $t1, 0($sp)
        j     next

# ---- + ( a b -- a+b ) --------------------------------------------------
code_ADD:
        lw    $t1, 0($sp)             # pop b
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)             # a stays: replace it with a+b
        addu  $t2, $t2, $t1
        sw    $t2, 0($sp)
        j     next

# ---- - ( a b -- a-b ) --------------------------------------------------
code_SUB:
        lw    $t1, 0($sp)             # pop b
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)
        subu  $t2, $t2, $t1
        sw    $t2, 0($sp)
        j     next

# ---- * ( a b -- a*b ) --------------------------------------------------
code_MUL:
        lw    $t1, 0($sp)             # pop b
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)
        mul   $t2, $t2, $t1
        sw    $t2, 0($sp)
        j     next

# ---- /MOD ( a b -- remainder quotient ): signed, truncating ------------
code_DIVMOD:
        lw    $t1, 0($sp)             # b
        lw    $t2, 4($sp)             # a
        div   $t2, $t1                # lo = a / b, hi = a % b
        mfhi  $t3
        sw    $t3, 4($sp)             # remainder where a was
        mflo  $t3
        sw    $t3, 0($sp)             # quotient on top
        j     next

# ======================================================================
#  COMPARISONS  ( a b -- flag ) and ( a -- flag ): flag is 1 or 0
# ======================================================================

# ---- = ( a b -- a=b ) --------------------------------------------------
code_EQU:
        lw    $t1, 0($sp)             # pop b
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)             # a
        xor   $t2, $t2, $t1           # 0 exactly when a = b
        sltiu $t2, $t2, 1
        sw    $t2, 0($sp)
        j     next

# ---- <> ( a b -- a<>b ) ------------------------------------------------
code_NEQU:
        lw    $t1, 0($sp)
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)
        xor   $t2, $t2, $t1
        sltu  $t2, $zero, $t2         # 1 when the xor is non-zero
        sw    $t2, 0($sp)
        j     next

# ---- < ( a b -- a<b ) --------------------------------------------------
code_LT:
        lw    $t1, 0($sp)
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)
        slt   $t2, $t2, $t1
        sw    $t2, 0($sp)
        j     next

# ---- > ( a b -- a>b ) --------------------------------------------------
code_GT:
        lw    $t1, 0($sp)
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)
        slt   $t2, $t1, $t2           # a > b is b < a
        sw    $t2, 0($sp)
        j     next

# ---- <= ( a b -- a<=b ) ------------------------------------------------
code_LE:
        lw    $t1, 0($sp)
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)
        slt   $t2, $t1, $t2           # a > b ...
        xori  $t2, $t2, 1             # ... and not that
        sw    $t2, 0($sp)
        j     next

# ---- >= ( a b -- a>=b ) ------------------------------------------------
code_GE:
        lw    $t1, 0($sp)
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)
        slt   $t2, $t2, $t1           # a < b ...
        xori  $t2, $t2, 1             # ... and not that
        sw    $t2, 0($sp)
        j     next

# ---- 0= ( a -- a=0 ) ---------------------------------------------------
code_ZEQU:
        lw    $t1, 0($sp)
        sltiu $t1, $t1, 1
        sw    $t1, 0($sp)
        j     next

# ---- 0<> ( a -- a<>0 ) -------------------------------------------------
code_ZNEQU:
        lw    $t1, 0($sp)
        sltu  $t1, $zero, $t1
        sw    $t1, 0($sp)
        j     next

# ---- 0< ( a -- a<0 ) ---------------------------------------------------
code_ZLT:
        lw    $t1, 0($sp)
        slt   $t1, $t1, $zero
        sw    $t1, 0($sp)
        j     next

# ---- 0> ( a -- a>0 ) ---------------------------------------------------
code_ZGT:
        lw    $t1, 0($sp)
        slt   $t1, $zero, $t1
        sw    $t1, 0($sp)
        j     next

# ---- 0<= ( a -- a<=0 ) -------------------------------------------------
code_ZLE:
        lw    $t1, 0($sp)
        slt   $t1, $zero, $t1         # a > 0 ...
        xori  $t1, $t1, 1             # ... and not that
        sw    $t1, 0($sp)
        j     next

# ---- 0>= ( a -- a>=0 ) -------------------------------------------------
code_ZGE:
        lw    $t1, 0($sp)
        slt   $t1, $t1, $zero         # a < 0 ...
        xori  $t1, $t1, 1             # ... and not that
        sw    $t1, 0($sp)
        j     next

# ======================================================================
#  BITWISE  (bit by bit, so 1/0 flags combine as and/or/xor)
# ======================================================================

# ---- AND ( a b -- a&b ) ------------------------------------------------
code_AND:
        lw    $t1, 0($sp)
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)
        and   $t2, $t2, $t1
        sw    $t2, 0($sp)
        j     next

# ---- OR ( a b -- a|b ) -------------------------------------------------
code_OR:
        lw    $t1, 0($sp)
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)
        or    $t2, $t2, $t1
        sw    $t2, 0($sp)
        j     next

# ---- XOR ( a b -- a^b ) ------------------------------------------------
code_XOR:
        lw    $t1, 0($sp)
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)
        xor   $t2, $t2, $t1
        sw    $t2, 0($sp)
        j     next

# ---- INVERT ( a -- ~a ) ------------------------------------------------
code_INVERT:
        lw    $t1, 0($sp)
        nor   $t1, $t1, $zero
        sw    $t1, 0($sp)
        j     next

# ======================================================================
#  MEMORY
# ======================================================================

# ---- ! ( n addr -- ): store the cell n at addr -------------------------
code_STORE:
        lw    $t1, 0($sp)             # addr
        lw    $t2, 4($sp)             # n
        addiu $sp, $sp, 8
        sw    $t2, 0($t1)
        j     next

# ---- @ ( addr -- n ): fetch the cell at addr ---------------------------
code_FETCH:
        lw    $t1, 0($sp)
        lw    $t1, 0($t1)
        sw    $t1, 0($sp)
        j     next

# ---- +! ( n addr -- ): add n to the cell at addr -----------------------
code_ADDSTORE:
        lw    $t1, 0($sp)             # addr
        lw    $t2, 4($sp)             # n
        addiu $sp, $sp, 8
        lw    $t3, 0($t1)
        addu  $t3, $t3, $t2
        sw    $t3, 0($t1)
        j     next

# ---- -! ( n addr -- ): subtract n from the cell at addr ----------------
code_SUBSTORE:
        lw    $t1, 0($sp)             # addr
        lw    $t2, 4($sp)             # n
        addiu $sp, $sp, 8
        lw    $t3, 0($t1)
        subu  $t3, $t3, $t2
        sw    $t3, 0($t1)
        j     next

# ---- C! ( c addr -- ): store the low byte of c at addr -----------------
code_STOREBYTE:
        lw    $t1, 0($sp)             # addr
        lw    $t2, 4($sp)             # c
        addiu $sp, $sp, 8
        sb    $t2, 0($t1)
        j     next

# ---- C@ ( addr -- c ): fetch the byte at addr, zero-extended -----------
code_FETCHBYTE:
        lw    $t1, 0($sp)
        lbu   $t1, 0($t1)
        sw    $t1, 0($sp)
        j     next

# ---- CMOVE ( src dest len -- ): copy len bytes, lowest address first ---
code_CMOVE:
        lw    $t1, 0($sp)             # len
        lw    $t2, 4($sp)             # dest
        lw    $t3, 8($sp)             # src
        addiu $sp, $sp, 12
cmove_loop:
        beqz  $t1, cmove_done
        lbu   $t4, 0($t3)
        sb    $t4, 0($t2)
        addiu $t3, $t3, 1
        addiu $t2, $t2, 1
        addiu $t1, $t1, -1
        j     cmove_loop
cmove_done:
        j     next

# ======================================================================
#  THE RETURN STACK AND THE DATA STACK, AS VALUES
# ======================================================================

# ---- >R ( a -- ) R:( -- a ): move a to the return stack ----------------
code_TOR:
        lw    $t1, 0($sp)
        addiu $sp, $sp, 4
        addiu $s6, $s6, -4
        sw    $t1, 0($s6)
        j     next

# ---- R> ( -- a ) R:( a -- ): move it back ------------------------------
code_FROMR:
        lw    $t1, 0($s6)
        addiu $s6, $s6, 4
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
        j     next

# ---- RSP@ ( -- rsp ): push the return stack pointer --------------------
code_RSPFETCH:
        addiu $sp, $sp, -4
        sw    $s6, 0($sp)
        j     next

# ---- RSP! ( rsp -- ): set the return stack pointer ---------------------
code_RSPSTORE:
        lw    $s6, 0($sp)
        addiu $sp, $sp, 4
        j     next

# ---- RDROP ( -- ) R:( a -- ) -------------------------------------------
code_RDROP:
        addiu $s6, $s6, 4
        j     next

# ---- DSP@ ( -- dsp ): push the data stack pointer, as it was before ----
code_DSPFETCH:
        move  $t1, $sp
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
        j     next

# ---- DSP! ( dsp -- ): set the data stack pointer -----------------------
code_DSPSTORE:
        lw    $sp, 0($sp)
        j     next

# ======================================================================
#  BRANCHES: the cell after BRANCH is a byte offset, added to the address
#  of that offset cell (so -8 jumps back two cells, to the cell before
#  BRANCH).  Offsets are relative, so a thread works wherever it lives.
# ======================================================================

# ---- BRANCH ( -- ): IP = IP + offset -----------------------------------
code_BRANCH:
        lw    $t1, 0($s7)             # the offset cell
        addu  $s7, $s7, $t1
        j     next

# ---- 0BRANCH ( flag -- ): branch if flag is 0, else skip the offset ----
code_ZBRANCH:
        lw    $t1, 0($sp)
        addiu $sp, $sp, 4
        beqz  $t1, code_BRANCH
        addiu $s7, $s7, 4             # step over the offset cell
        j     next

# ======================================================================
#  OUTPUT AND EXIT
# ======================================================================

# ---- . ( n -- ): print n and a space -----------------------------------
code_DOT:
        lw    $a0, 0($sp)             # pop n
        addiu $sp, $sp, 4
        li    $v0, 1                  # print_int
        syscall
        li    $a0, ' '
        li    $v0, 11                 # print_char
        syscall
        j     next

# ---- CR ( -- ): print a newline ----------------------------------------
code_CR:
        li    $a0, 10
        li    $v0, 11                 # print_char('\n')
        syscall
        j     next

# ---- BYE ( -- ): exit the program --------------------------------------
code_BYE:
        li    $v0, 10                 # exit
        syscall
