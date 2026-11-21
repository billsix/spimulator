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

# Invocation (Forth words on stdin):
#   printf 'SQAURE\n' | spimulator -f forth-7-quit.asm   ->  PARSE ERROR: SQAURE

# PURPOSE:  Rung 7 of the Forth ladder — the OUTER INTERPRETER as
#           jonesforth writes it.  In rung 6 (forth-6-compiling.asm)
#           INTERPRET was 45 hand-threaded cells.  Here it is one
#           primitive in machine code, running the same rule (find, else
#           number; run, else compile), and QUIT, its caller, becomes
#
#             : QUIT  R0 RSP!  BEGIN INTERPRET AGAIN ;
#
#           R0 RSP! empties the return stack, so whatever a word left there
#           is gone.  A word that is neither in the dictionary nor a number
#           is now reported as `PARSE ERROR: <word>`, and the interpreter
#           goes on with the next word (jonesforth writes the message to
#           stderr; spim's print syscalls write stdout, so it is part of
#           the golden output here).
#
#           New: INTERPRET in asm, R0 and RSP! at the top of QUIT, the
#           PARSE ERROR message, TELL, CHAR, S0 (the empty data stack's
#           address, for rung 8's .S).
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
#  $t1-$t9  | —          | scratch inside one primitive
#  $a0-$a1  | —          | arguments of the jal helpers (_FIND, _NUMBER, ...)
#  $v0-$v1  | —          | their results; also the syscall number
#
#  Truth values are jonesforth's 1 (true) and 0 (false), not ANS Forth's
#  -1 and 0.

        .data

# ---- start-up: IP begins here, and QUIT never returns ------------------
cold_start:
        .word QUIT

# ---- the built-in dictionary, oldest word first ------------------------
name_DROP:
        .word 0
        .byte 4                         # length 4
        .ascii "DROP"
        .align 2
DROP:      .word code_DROP

name_SWAP:
        .word name_DROP
        .byte 4                         # length 4
        .ascii "SWAP"
        .align 2
SWAP:      .word code_SWAP

name_DUP:
        .word name_SWAP
        .byte 3                         # length 3
        .ascii "DUP"
        .align 2
DUP:       .word code_DUP

name_OVER:
        .word name_DUP
        .byte 4                         # length 4
        .ascii "OVER"
        .align 2
OVER:      .word code_OVER

name_ROT:
        .word name_OVER
        .byte 3                         # length 3
        .ascii "ROT"
        .align 2
ROT:       .word code_ROT

name_NROT:
        .word name_ROT
        .byte 4                         # length 4
        .ascii "-ROT"
        .align 2
NROT:      .word code_NROT

name_TWODROP:
        .word name_NROT
        .byte 5                         # length 5
        .ascii "2DROP"
        .align 2
TWODROP:   .word code_TWODROP

name_TWODUP:
        .word name_TWODROP
        .byte 4                         # length 4
        .ascii "2DUP"
        .align 2
TWODUP:    .word code_TWODUP

name_TWOSWAP:
        .word name_TWODUP
        .byte 5                         # length 5
        .ascii "2SWAP"
        .align 2
TWOSWAP:   .word code_TWOSWAP

name_QDUP:
        .word name_TWOSWAP
        .byte 4                         # length 4
        .ascii "?DUP"
        .align 2
QDUP:      .word code_QDUP

name_INCR:
        .word name_QDUP
        .byte 2                         # length 2
        .ascii "1+"
        .align 2
INCR:      .word code_INCR

name_DECR:
        .word name_INCR
        .byte 2                         # length 2
        .ascii "1-"
        .align 2
DECR:      .word code_DECR

name_INCR4:
        .word name_DECR
        .byte 2                         # length 2
        .ascii "4+"
        .align 2
INCR4:     .word code_INCR4

name_DECR4:
        .word name_INCR4
        .byte 2                         # length 2
        .ascii "4-"
        .align 2
DECR4:     .word code_DECR4

name_ADD:
        .word name_DECR4
        .byte 1                         # length 1
        .ascii "+"
        .align 2
ADD:       .word code_ADD

name_SUB:
        .word name_ADD
        .byte 1                         # length 1
        .ascii "-"
        .align 2
SUB:       .word code_SUB

name_MUL:
        .word name_SUB
        .byte 1                         # length 1
        .ascii "*"
        .align 2
MUL:       .word code_MUL

name_DIVMOD:
        .word name_MUL
        .byte 4                         # length 4
        .ascii "/MOD"
        .align 2
DIVMOD:    .word code_DIVMOD

name_EQU:
        .word name_DIVMOD
        .byte 1                         # length 1
        .ascii "="
        .align 2
EQU:       .word code_EQU

name_NEQU:
        .word name_EQU
        .byte 2                         # length 2
        .ascii "<>"
        .align 2
NEQU:      .word code_NEQU

name_LT:
        .word name_NEQU
        .byte 1                         # length 1
        .ascii "<"
        .align 2
LT:        .word code_LT

name_GT:
        .word name_LT
        .byte 1                         # length 1
        .ascii ">"
        .align 2
GT:        .word code_GT

name_LE:
        .word name_GT
        .byte 2                         # length 2
        .ascii "<="
        .align 2
LE:        .word code_LE

name_GE:
        .word name_LE
        .byte 2                         # length 2
        .ascii ">="
        .align 2
GE:        .word code_GE

name_ZEQU:
        .word name_GE
        .byte 2                         # length 2
        .ascii "0="
        .align 2
ZEQU:      .word code_ZEQU

name_ZNEQU:
        .word name_ZEQU
        .byte 3                         # length 3
        .ascii "0<>"
        .align 2
ZNEQU:     .word code_ZNEQU

name_ZLT:
        .word name_ZNEQU
        .byte 2                         # length 2
        .ascii "0<"
        .align 2
ZLT:       .word code_ZLT

name_ZGT:
        .word name_ZLT
        .byte 2                         # length 2
        .ascii "0>"
        .align 2
ZGT:       .word code_ZGT

name_ZLE:
        .word name_ZGT
        .byte 3                         # length 3
        .ascii "0<="
        .align 2
ZLE:       .word code_ZLE

name_ZGE:
        .word name_ZLE
        .byte 3                         # length 3
        .ascii "0>="
        .align 2
ZGE:       .word code_ZGE

name_AND:
        .word name_ZGE
        .byte 3                         # length 3
        .ascii "AND"
        .align 2
AND:       .word code_AND

name_OR:
        .word name_AND
        .byte 2                         # length 2
        .ascii "OR"
        .align 2
OR:        .word code_OR

name_XOR:
        .word name_OR
        .byte 3                         # length 3
        .ascii "XOR"
        .align 2
XOR:       .word code_XOR

name_INVERT:
        .word name_XOR
        .byte 6                         # length 6
        .ascii "INVERT"
        .align 2
INVERT:    .word code_INVERT

name_EXIT:
        .word name_INVERT
        .byte 4                         # length 4
        .ascii "EXIT"
        .align 2
EXIT:      .word code_EXIT

name_LIT:
        .word name_EXIT
        .byte 3                         # length 3
        .ascii "LIT"
        .align 2
LIT:       .word code_LIT

name_STORE:
        .word name_LIT
        .byte 1                         # length 1
        .ascii "!"
        .align 2
STORE:     .word code_STORE

name_FETCH:
        .word name_STORE
        .byte 1                         # length 1
        .ascii "@"
        .align 2
FETCH:     .word code_FETCH

name_ADDSTORE:
        .word name_FETCH
        .byte 2                         # length 2
        .ascii "+!"
        .align 2
ADDSTORE:  .word code_ADDSTORE

name_SUBSTORE:
        .word name_ADDSTORE
        .byte 2                         # length 2
        .ascii "-!"
        .align 2
SUBSTORE:  .word code_SUBSTORE

name_STOREBYTE:
        .word name_SUBSTORE
        .byte 2                         # length 2
        .ascii "C!"
        .align 2
STOREBYTE: .word code_STOREBYTE

name_FETCHBYTE:
        .word name_STOREBYTE
        .byte 2                         # length 2
        .ascii "C@"
        .align 2
FETCHBYTE: .word code_FETCHBYTE

name_CMOVE:
        .word name_FETCHBYTE
        .byte 5                         # length 5
        .ascii "CMOVE"
        .align 2
CMOVE:     .word code_CMOVE

name_STATE:
        .word name_CMOVE
        .byte 5                         # length 5
        .ascii "STATE"
        .align 2
STATE:     .word code_STATE

name_HERE:
        .word name_STATE
        .byte 4                         # length 4
        .ascii "HERE"
        .align 2
HERE:      .word code_HERE

name_LATEST:
        .word name_HERE
        .byte 6                         # length 6
        .ascii "LATEST"
        .align 2
LATEST:    .word code_LATEST

name_SZ:
        .word name_LATEST
        .byte 2                         # length 2
        .ascii "S0"
        .align 2
SZ:        .word code_SZ

name_BASE:
        .word name_SZ
        .byte 4                         # length 4
        .ascii "BASE"
        .align 2
BASE:      .word code_BASE

name_RZ:
        .word name_BASE
        .byte 2                         # length 2
        .ascii "R0"
        .align 2
RZ:        .word code_RZ

name_TOR:
        .word name_RZ
        .byte 2                         # length 2
        .ascii ">R"
        .align 2
TOR:       .word code_TOR

name_FROMR:
        .word name_TOR
        .byte 2                         # length 2
        .ascii "R>"
        .align 2
FROMR:     .word code_FROMR

name_RSPFETCH:
        .word name_FROMR
        .byte 4                         # length 4
        .ascii "RSP@"
        .align 2
RSPFETCH:  .word code_RSPFETCH

name_RSPSTORE:
        .word name_RSPFETCH
        .byte 4                         # length 4
        .ascii "RSP!"
        .align 2
RSPSTORE:  .word code_RSPSTORE

name_RDROP:
        .word name_RSPSTORE
        .byte 5                         # length 5
        .ascii "RDROP"
        .align 2
RDROP:     .word code_RDROP

name_DSPFETCH:
        .word name_RDROP
        .byte 4                         # length 4
        .ascii "DSP@"
        .align 2
DSPFETCH:  .word code_DSPFETCH

name_DSPSTORE:
        .word name_DSPFETCH
        .byte 4                         # length 4
        .ascii "DSP!"
        .align 2
DSPSTORE:  .word code_DSPSTORE

name_KEY:
        .word name_DSPSTORE
        .byte 3                         # length 3
        .ascii "KEY"
        .align 2
KEY:       .word code_KEY

name_EMIT:
        .word name_KEY
        .byte 4                         # length 4
        .ascii "EMIT"
        .align 2
EMIT:      .word code_EMIT

name_WORD:
        .word name_EMIT
        .byte 4                         # length 4
        .ascii "WORD"
        .align 2
WORD:      .word code_WORD

name_NUMBER:
        .word name_WORD
        .byte 6                         # length 6
        .ascii "NUMBER"
        .align 2
NUMBER:    .word code_NUMBER

name_FIND:
        .word name_NUMBER
        .byte 4                         # length 4
        .ascii "FIND"
        .align 2
FIND:      .word code_FIND

name_TCFA:
        .word name_FIND
        .byte 4                         # length 4
        .ascii ">CFA"
        .align 2
TCFA:      .word code_TCFA

name_CREATE:
        .word name_TCFA
        .byte 6                         # length 6
        .ascii "CREATE"
        .align 2
CREATE:    .word code_CREATE

name_COMMA:
        .word name_CREATE
        .byte 1                         # length 1
        .ascii ","
        .align 2
COMMA:     .word code_COMMA

name_LBRAC:
        .word name_COMMA
        .byte 129                       # length 1 | F_IMMED (128)
        .ascii "["
        .align 2
LBRAC:     .word code_LBRAC

name_RBRAC:
        .word name_LBRAC
        .byte 1                         # length 1
        .ascii "]"
        .align 2
RBRAC:     .word code_RBRAC

name_COLON:
        .word name_RBRAC
        .byte 1                         # length 1
        .ascii ":"
        .align 2
COLON:     .word docol
        .word WORD, CREATE            # the header, named by the next word
        .word LIT, docol, COMMA       # the codeword: DOCOL
        .word LATEST, FETCH, HIDDEN   # hide the word until ; ends it
        .word RBRAC, EXIT             # STATE = 1: compile

name_SEMICOLON:
        .word name_COLON
        .byte 129                       # length 1 | F_IMMED (128)
        .ascii ";"
        .align 2
SEMICOLON: .word docol
        .word LIT, EXIT, COMMA        # end the body with EXIT
        .word LATEST, FETCH, HIDDEN   # unhide the word
        .word LBRAC, EXIT             # STATE = 0: interpret

name_IMMEDIATE:
        .word name_SEMICOLON
        .byte 137                       # length 9 | F_IMMED (128)
        .ascii "IMMEDIATE"
        .align 2
IMMEDIATE: .word code_IMMEDIATE

name_HIDDEN:
        .word name_IMMEDIATE
        .byte 6                         # length 6
        .ascii "HIDDEN"
        .align 2
HIDDEN:    .word code_HIDDEN

name_HIDE:
        .word name_HIDDEN
        .byte 4                         # length 4
        .ascii "HIDE"
        .align 2
HIDE:      .word docol
        .word WORD, FIND, HIDDEN, EXIT

name_TICK:
        .word name_HIDE
        .byte 1                         # length 1
        .ascii "'"
        .align 2
TICK:      .word code_TICK

name_BRANCH:
        .word name_TICK
        .byte 6                         # length 6
        .ascii "BRANCH"
        .align 2
BRANCH:    .word code_BRANCH

name_ZBRANCH:
        .word name_BRANCH
        .byte 7                         # length 7
        .ascii "0BRANCH"
        .align 2
ZBRANCH:   .word code_ZBRANCH

name_TELL:
        .word name_ZBRANCH
        .byte 4                         # length 4
        .ascii "TELL"
        .align 2
TELL:      .word code_TELL

name_QUIT:
        .word name_TELL
        .byte 4                         # length 4
        .ascii "QUIT"
        .align 2
QUIT:      .word docol
        .word RZ, RSPSTORE            # R0 RSP!: empty the return stack
        .word INTERPRET, BRANCH, -8   # loop forever

name_INTERPRET:
        .word name_QUIT
        .byte 9                         # length 9
        .ascii "INTERPRET"
        .align 2
INTERPRET: .word code_INTERPRET

name_CHAR:
        .word name_INTERPRET
        .byte 4                         # length 4
        .ascii "CHAR"
        .align 2
CHAR:      .word code_CHAR

name_EXECUTE:
        .word name_CHAR
        .byte 7                         # length 7
        .ascii "EXECUTE"
        .align 2
EXECUTE:   .word code_EXECUTE

name_DOT:
        .word name_EXECUTE
        .byte 1                         # length 1
        .ascii "."
        .align 2
DOT:       .word code_DOT

name_CR:
        .word name_DOT
        .byte 2                         # length 2
        .ascii "CR"
        .align 2
CR:        .word code_CR

name_BYE:
        .word name_CR
        .byte 3                         # length 3
        .ascii "BYE"
        .align 2
BYE:       .word code_BYE

# ---- variables ---------------------------------------------------------
var_LATEST:
        .word name_BYE               # the newest header
var_BASE:
        .word 10                      # numbers are read in base BASE
var_STATE:
        .word 0                       # 0 = interpreting, 1 = compiling
var_HERE:
        .word 0                       # the next free byte; set by main
var_S0:
        .word 0                       # the empty stack's $sp; set by main

parse_error:
        .asciiz "PARSE ERROR: "

# ---- WORD's buffer: the name just read (31 bytes at most) --------------
word_buffer:
        .space 32

# ---- the return stack: 256 saved IPs, growing down from the top --------
return_stack:
        .space 1024
return_stack_top:
        .word 0

        .text
        .globl main
main:
        la    $s6, return_stack_top   # RSP = empty return stack
        li    $a0, 65536              # 64 KiB for the words the user defines
        li    $v0, 9                  # sbrk
        syscall
        sw    $v0, var_HERE           # HERE = the start of that memory
        sw    $sp, var_S0             # S0 = the empty data stack
        la    $s7, cold_start         # IP = the start-up thread
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
#  INPUT AND OUTPUT
#
#  The helpers _KEY, _WORD, _FIND and _TCFA are ordinary jal subroutines
#  (arguments in $a0/$a1, results in $v0/$v1), so primitives can share
#  them.  _KEY is a leaf.  _WORD calls _KEY, so it keeps its $ra on the
#  data stack for the duration, as an i386 `call` does in jonesforth.
# ======================================================================

# ---- _KEY -> $v0: the next byte of stdin; at end of input, exit 0 -----
_KEY:
        li    $v0, 12                 # read_char
        syscall
        bltz  $v0, key_eof            # -1: end of input
        jr    $ra
key_eof:
        li    $v0, 10                 # exit: the program ends with its input
        syscall

# ---- KEY ( -- c ) -----------------------------------------------------
code_KEY:
        jal   _KEY
        addiu $sp, $sp, -4
        sw    $v0, 0($sp)
        j     next

# ---- EMIT ( c -- ): print one character ---------------------------------
code_EMIT:
        lw    $a0, 0($sp)
        addiu $sp, $sp, 4
        li    $v0, 11                 # print_char
        syscall
        j     next

# ---- _WORD -> $v0 = address, $v1 = length of the next word ------------
# Skips blanks (any byte <= ' ') and `\` comments to the end of the line,
# then copies bytes up to the next blank into word_buffer.  The buffer is
# reused by the next call.
_WORD:
        addiu $sp, $sp, -4            # keep $ra across the _KEY calls
        sw    $ra, 0($sp)
word_skip:
        jal   _KEY
        li    $t1, 92                 # '\': a comment to the end of the line
        beq   $v0, $t1, word_comment
        sltiu $t1, $v0, 33            # a blank?
        bnez  $t1, word_skip
        la    $t2, word_buffer        # $t2 = where the next byte goes
word_store:
        sb    $v0, 0($t2)
        addiu $t2, $t2, 1
        jal   _KEY
        sltiu $t1, $v0, 33            # stop at the first blank
        beqz  $t1, word_store
        la    $v0, word_buffer
        subu  $v1, $t2, $v0           # length = bytes stored
        lw    $ra, 0($sp)
        addiu $sp, $sp, 4
        jr    $ra
word_comment:
        jal   _KEY
        li    $t1, 10                 # skip to the newline
        bne   $v0, $t1, word_comment
        j     word_skip

# ---- WORD ( -- addr len ) -----------------------------------------------
code_WORD:
        jal   _WORD
        addiu $sp, $sp, -8
        sw    $v0, 4($sp)             # addr
        sw    $v1, 0($sp)             # len on top
        j     next

# ======================================================================
#  THE DICTIONARY
#
#  Every word has a header, laid out as in jonesforth:
#
#    +------+-----+---+---+---+-----+------------+------------- - -
#    | link | len | D | U | P | pad | codeword   | body (Forth words)
#    +------+-----+---+---+---+-----+------------+------------- - -
#     4 bytes 1    the name, 0-31   aligned to 4
#                  bytes
#
#  `link` is the address of the previous word's header (0 for the first),
#  so the dictionary is a linked list, newest first, starting at LATEST.
#  The len byte also holds two flags, F_IMMED (0x80) and F_HIDDEN
#  (0x20); the low five bits are the length.
# ======================================================================

# ---- _FIND ($a0 = addr, $a1 = len) -> $v0 = header, or 0 ---------------
# Walks the list from LATEST.  The length compare masks in F_HIDDEN, so a
# hidden word looks the wrong length and is never found.
_FIND:
        lw    $t1, var_LATEST         # $t1 = the header being tried
find_loop:
        beqz  $t1, find_missing       # end of the list
        lbu   $t2, 4($t1)             # the length/flags byte
        andi  $t2, $t2, 0x3f          # F_HIDDEN | the length
        bne   $t2, $a1, find_next
        move  $t3, $a0                # compare the names byte by byte
        addiu $t4, $t1, 5             # the header's name
        move  $t5, $a1                # bytes left
find_compare:
        beqz  $t5, find_found
        lbu   $t6, 0($t3)
        lbu   $t7, 0($t4)
        bne   $t6, $t7, find_next
        addiu $t3, $t3, 1
        addiu $t4, $t4, 1
        addiu $t5, $t5, -1
        j     find_compare
find_found:
        move  $v0, $t1
        jr    $ra
find_next:
        lw    $t1, 0($t1)             # follow the link
        j     find_loop
find_missing:
        li    $v0, 0
        jr    $ra

# ---- FIND ( addr len -- header|0 ) ------------------------------------
code_FIND:
        lw    $a1, 0($sp)             # len
        lw    $a0, 4($sp)             # addr
        addiu $sp, $sp, 4
        jal   _FIND
        sw    $v0, 0($sp)
        j     next

# ---- _TCFA ($a0 = header) -> $v0 = its codeword address ---------------
_TCFA:
        lbu   $t1, 4($a0)             # the length/flags byte
        andi  $t1, $t1, 0x1f          # F_LENMASK: just the length
        addu  $v0, $a0, $t1
        addiu $v0, $v0, 8             # skip link (4) + len (1), round up:
        li    $t1, -4                 #   (header + 5 + len + 3) & ~3
        and   $v0, $v0, $t1
        jr    $ra

# ---- >CFA ( header -- xt ): the codeword address ("execution token") --
code_TCFA:
        lw    $a0, 0($sp)
        jal   _TCFA
        sw    $v0, 0($sp)
        j     next

# ---- EXECUTE ( xt -- ): run the word whose codeword is at xt -----------
# The second half of NEXT, with W taken from the stack instead of the
# thread.  When the word finishes, its NEXT carries on after EXECUTE.
code_EXECUTE:
        lw    $t0, 0($sp)             # W = xt
        addiu $sp, $sp, 4
        lw    $t1, 0($t0)
        jr    $t1

# ---- LATEST ( -- addr ): the variable holding the newest header ------
code_LATEST:
        la    $t1, var_LATEST
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
        j     next

# ======================================================================
#  NUMBERS
# ======================================================================

# ---- _NUMBER ($a0 = addr, $a1 = len) -> $v0 = n, $v1 = unparsed -------
# Parses an optional '-' and then digits in BASE (0-9, then A-Z for 10-35).
# Stops at the first byte that is not a digit in BASE; $v1 is the count
# of bytes left over, so 0 means the whole word was a number.  As in
# jonesforth, the empty string parses as 0 and a lone "-" is an error.
_NUMBER:
        li    $v0, 0                  # n = 0
        move  $v1, $a1                # bytes left
        beqz  $v1, number_done
        lw    $t1, var_BASE
        li    $t2, 0                  # negative? (0 = no)
        lbu   $t3, 0($a0)
        li    $t4, 45                 # '-'
        bne   $t3, $t4, number_digit
        li    $t2, 1                  # remember the sign, skip the '-'
        addiu $a0, $a0, 1
        addiu $v1, $v1, -1
        bnez  $v1, number_digit
        li    $v1, 1                  # just "-": one byte unparsed
        jr    $ra
number_digit:
        beqz  $v1, number_sign        # every byte used
        lbu   $t3, 0($a0)
        addiu $t3, $t3, -48           # '0'..'9' -> 0..9
        bltz  $t3, number_sign        # below '0': stop
        sltiu $t4, $t3, 10
        bnez  $t4, number_inbase
        addiu $t3, $t3, -17           # 'A' is '0' + 17
        bltz  $t3, number_sign        # between '9' and 'A': stop
        addiu $t3, $t3, 10            # 'A'..'Z' -> 10..35
number_inbase:
        slt   $t4, $t3, $t1           # digit < BASE?
        beqz  $t4, number_sign
        mul   $v0, $v0, $t1           # n = n * BASE + digit
        addu  $v0, $v0, $t3
        addiu $a0, $a0, 1
        addiu $v1, $v1, -1
        j     number_digit
number_sign:
        beqz  $t2, number_done
        subu  $v0, $zero, $v0         # n = -n
number_done:
        jr    $ra

# ---- NUMBER ( addr len -- n unparsed ) --------------------------------
code_NUMBER:
        lw    $a1, 0($sp)             # len
        lw    $a0, 4($sp)             # addr
        jal   _NUMBER
        sw    $v0, 4($sp)             # n
        sw    $v1, 0($sp)             # unparsed count on top
        j     next

# ---- BASE ( -- addr ): the variable holding the number base ----------
code_BASE:
        la    $t1, var_BASE
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
        j     next

# ======================================================================
#  COMPILING
#
#  User words are built at HERE, in memory taken from the heap with sbrk
#  at start-up.  STATE is 0 while interpreting and 1 while compiling.
# ======================================================================

# ---- STATE ( -- addr ) and HERE ( -- addr ): the two variables --------
code_STATE:
        la    $t1, var_STATE
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
        j     next

code_HERE:
        la    $t1, var_HERE
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
        j     next

# ---- CREATE ( addr len -- ): a new header at HERE, named addr/len ----
# Writes the link (the old LATEST), the length byte and the name, rounds
# HERE up to a cell, and makes the new header LATEST.  The codeword and
# body are appended after it with `,`.
code_CREATE:
        lw    $t1, 0($sp)             # len
        lw    $t2, 4($sp)             # addr
        addiu $sp, $sp, 8
        lw    $t3, var_HERE           # $t3 = the new header
        lw    $t4, var_LATEST
        sw    $t4, 0($t3)             # link to the previous newest word
        sb    $t1, 4($t3)             # the length, no flags
        addiu $t5, $t3, 5             # $t5 = where the name goes
create_copy:
        beqz  $t1, create_align
        lbu   $t6, 0($t2)
        sb    $t6, 0($t5)
        addiu $t2, $t2, 1
        addiu $t5, $t5, 1
        addiu $t1, $t1, -1
        j     create_copy
create_align:
        addiu $t5, $t5, 3             # round up to the next cell
        li    $t6, -4
        and   $t5, $t5, $t6
        sw    $t3, var_LATEST
        sw    $t5, var_HERE
        j     next

# ---- _COMMA ($a0 = n): append the cell n at HERE ----------------------
_COMMA:
        lw    $t1, var_HERE
        sw    $a0, 0($t1)
        addiu $t1, $t1, 4
        sw    $t1, var_HERE
        jr    $ra

# ---- , ( n -- ): append n to the word being compiled ------------------
code_COMMA:
        lw    $a0, 0($sp)
        addiu $sp, $sp, 4
        jal   _COMMA
        j     next

# ---- [ ( -- ): STATE = 0, interpret (an IMMEDIATE word) ---------------
code_LBRAC:
        sw    $zero, var_STATE
        j     next

# ---- ] ( -- ): STATE = 1, compile ------------------------------------
code_RBRAC:
        li    $t1, 1
        sw    $t1, var_STATE
        j     next

# ---- IMMEDIATE ( -- ): toggle F_IMMED on the newest word --------------
code_IMMEDIATE:
        lw    $t1, var_LATEST
        lbu   $t2, 4($t1)
        xori  $t2, $t2, 0x80          # F_IMMED
        sb    $t2, 4($t1)
        j     next

# ---- HIDDEN ( header -- ): toggle F_HIDDEN on a word ------------------
# : hides the word it is compiling, so a definition can call an older word
# of the same name; ; unhides it.
code_HIDDEN:
        lw    $t1, 0($sp)
        addiu $sp, $sp, 4
        lbu   $t2, 4($t1)
        xori  $t2, $t2, 0x20          # F_HIDDEN
        sb    $t2, 4($t1)
        j     next

# ---- ' ( -- xt ): push the next cell of the thread, and skip it -------
# Like LIT, so it works inside a compiled word: `' DUP` compiles to the
# cells ' DUP, and running them pushes DUP's codeword address.
code_TICK:
        lw    $t1, 0($s7)
        addiu $s7, $s7, 4
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
        j     next

# ======================================================================
#  THE OUTER INTERPRETER
# ======================================================================

# ---- INTERPRET ( -- ): read one word and run it, compile it, or push it
# The rule of rungs 4-6 in one primitive, as in jonesforth:
#   a word in the dictionary  -> run it if it is IMMEDIATE or STATE is 0,
#                                else append its codeword (xt) at HERE;
#   else a number             -> push it if STATE is 0, else append LIT n;
#   else                      -> print PARSE ERROR and the word.
# Running a word means jumping to it with W set, as NEXT would, so its own
# NEXT carries on in QUIT after INTERPRET.
code_INTERPRET:
        jal   _WORD                   # $v0 = addr, $v1 = len
        move  $a0, $v0                # $a0/$a1 = the word, kept for
        move  $a1, $v1                #   _NUMBER and the error message
        jal   _FIND
        beqz  $v0, interpret_number
        lbu   $t8, 4($v0)             # the flags, kept across _TCFA
        move  $a0, $v0
        jal   _TCFA                   # $v0 = xt
        andi  $t8, $t8, 0x80          # F_IMMED?
        bnez  $t8, interpret_execute
        lw    $t1, var_STATE
        beqz  $t1, interpret_execute
        move  $a0, $v0                # compiling: append the xt
        jal   _COMMA
        j     next
interpret_execute:
        move  $t0, $v0                # W = xt, then NEXT's second half
        lw    $t1, 0($t0)
        jr    $t1
interpret_number:
        jal   _NUMBER                 # $v0 = n, $v1 = bytes unparsed
        bnez  $v1, interpret_error
        lw    $t1, var_STATE
        beqz  $t1, interpret_push
        move  $t9, $v0                # compiling: append LIT n
        la    $a0, LIT
        jal   _COMMA
        move  $a0, $t9
        jal   _COMMA
        j     next
interpret_push:
        addiu $sp, $sp, -4
        sw    $v0, 0($sp)
        j     next
interpret_error:
        la    $a0, parse_error        # "PARSE ERROR: "
        li    $v0, 4                  # print_string
        syscall
        la    $t1, word_buffer        # then the word itself
        move  $t2, $a1
interpret_error_name:
        beqz  $t2, interpret_error_end
        lbu   $a0, 0($t1)
        li    $v0, 11
        syscall
        addiu $t1, $t1, 1
        addiu $t2, $t2, -1
        j     interpret_error_name
interpret_error_end:
        li    $a0, 10
        li    $v0, 11
        syscall
        j     next

# ---- TELL ( addr len -- ): print len bytes from addr -----------------
code_TELL:
        lw    $t2, 0($sp)             # len
        lw    $t1, 4($sp)             # addr
        addiu $sp, $sp, 8
tell_loop:
        beqz  $t2, tell_done
        lbu   $a0, 0($t1)
        li    $v0, 11                 # print_char
        syscall
        addiu $t1, $t1, 1
        addiu $t2, $t2, -1
        j     tell_loop
tell_done:
        j     next

# ---- CHAR ( -- c ): the first byte of the next word -------------------
code_CHAR:
        jal   _WORD
        lbu   $t1, 0($v0)
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
        j     next

# ---- S0 ( -- addr ): the variable holding the empty data stack's $sp --
code_SZ:
        la    $t1, var_S0
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
        j     next

# ---- R0 ( -- rsp ): the empty return stack's RSP, a constant ----------
code_RZ:
        la    $t1, return_stack_top
        addiu $sp, $sp, -4
        sw    $t1, 0($sp)
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
