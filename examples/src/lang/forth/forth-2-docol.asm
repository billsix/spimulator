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
#   spimulator -f forth-2-docol.asm   ->  49 / 27 / 81

# PURPOSE:  Rung 2 of the Forth ladder — WORDS WRITTEN IN FORTH, and the
#           RETURN STACK.  Rung 1 (forth-1-next.asm) could only run
#           primitives.  Here a word can itself be a thread of other words:
#
#               : SQUARE  DUP * ;          SQUARE: [ docol ][ &DUP ][ &* ][ &EXIT ]
#               : CUBE    DUP SQUARE * ;   CUBE:   [ docol ][ &DUP ][ &SQUARE ][ &* ][ &EXIT ]
#
#           (the `: ... ;` syntax arrives in rung 6; this rung writes the
#           cells by hand).  The codeword of a Forth-written word holds
#           DOCOL ("do colon"), which saves IP on the return stack and
#           points IP at the cell after the codeword (W + 4).  EXIT pops IP
#           back.  CUBE calls SQUARE, so two return addresses are saved at
#           once: the return stack is what makes nesting work.  The
#           program is `7 SQUARE . CR  3 CUBE . CR  3 SQUARE SQUARE . CR BYE`.
#
#           New: DOCOL, EXIT, DUP, *, the return stack ($s6).
#
#           INDIRECT THREADING (as jonesforth).  Each word has a CODEWORD: a
#           .data cell holding the address of the machine code that runs the
#           word.  The program list holds codeword ADDRESSES, so running a
#           word takes two loads:
#
#               IP --> [ &ADD ]          program cell   (the thread)
#                         |
#                         v
#                  ADD: [ code_ADD ]     codeword cell
#                         |
#                         v
#                  code_ADD: lw ... addu ... j next     machine code
#
#           The extra hop costs one load now and pays off in rung 2, where a
#           word written in Forth has the SAME codeword cell, holding DOCOL.
#
#           NEXT (jonesforth: `lodsl; jmp *(%eax)`):
#               lw    $t0, 0($s7)     # W  = *IP      (codeword address)
#               addiu $s7, $s7, 4     # IP = IP + 4   (step the thread)
#               lw    $t1, 0($t0)     # code address = *W
#               jr    $t1             # run the word
#           spim has no .macro, so NEXT is written once, at `next:`, and
#           every primitive ends with `j next`.
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
#  $t1-$t3  | —          | scratch inside one primitive
#
#  Truth values are jonesforth's 1 (true) and 0 (false), not ANS Forth's
#  -1 and 0 (from rung 3, where the comparisons arrive).

        .data

# ---- the program: 7 SQUARE . CR  3 CUBE . CR  3 SQUARE SQUARE . CR  BYE --
program:
        .word LIT, 7, SQUARE, DOT, CR
        .word LIT, 3, CUBE, DOT, CR
        .word LIT, 3, SQUARE, SQUARE, DOT, CR
        .word BYE

# ---- words written in Forth: codeword DOCOL, then the body's cells -----
SQUARE: .word docol                   # : SQUARE  DUP * ;
        .word DUP, MUL, EXIT
CUBE:   .word docol                   # : CUBE  DUP SQUARE * ;
        .word DUP, SQUARE, MUL, EXIT

# ---- codewords: one cell per primitive, holding its code address -------
EXIT:   .word code_EXIT
LIT:    .word code_LIT
DUP:    .word code_DUP
ADD:    .word code_ADD
MUL:    .word code_MUL
DOT:    .word code_DOT
CR:     .word code_CR
BYE:    .word code_BYE

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

# ---- DUP ( a -- a a ) --------------------------------------------------
code_DUP:
        lw    $t1, 0($sp)
        addiu $sp, $sp, -4
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

# ---- * ( a b -- a*b ) --------------------------------------------------
code_MUL:
        lw    $t1, 0($sp)             # pop b
        addiu $sp, $sp, 4
        lw    $t2, 0($sp)
        mul   $t2, $t2, $t1
        sw    $t2, 0($sp)
        j     next

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
