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


# C source — see hanoi.c
# Invocation:
#   spimulator -f hanoi.asm 3


#PURPOSE:  Print the move sequence that solves Towers of Hanoi for a
#          tower of N disks: A -> C using B as auxiliary.  Output is
#          2^N - 1 moves; classroom-friendly values are N=3 (7 moves)
#          through N=5 (31 moves).
#
#          STACK-VISIBLE STYLE: this is the deepest-frame demo.  hanoi
#          calls ITSELF twice, so all four C parameters (n, src, dst,
#          tmp) live in THIS call's own frame at fixed -N($fp) offsets,
#          alongside the saved $ra and $fp.  Between the two recursive
#          calls (which clobber $a0..$a3) every argument is reloaded
#          from its slot; the frame, not a register, is where a call's
#          state survives.  The base case n == 0 returns before any
#          frame is built.  main saves $ra (it jals) and homes n.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  argc        | —           | $a0        | int       |   4   | entry only; tested (== 2), then dead
#  argv        | —           | $a1        | char **   |   4   | entry only; argv[1] fetched for atoi
#  n           | -12($fp)    | $a0 (copy) | int       |   4   | parsed N; passed to hanoi(n,'A','C','B')
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | jal atoi / jal hanoi
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: hanoi — recursive; one frame PER call)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  (arg) n     | -12($fp)    | $a0 (copy) | int       |   4   | disks to move this level
#  (arg) src   | -16($fp)    | $a1 (copy) | char      |   4   | source peg ('A'/'B'/'C')
#  (arg) dst   | -20($fp)    | $a2 (copy) | char      |   4   | destination peg
#  (arg) tmp   | -24($fp)    | $a3 (copy) | char      |   4   | spare peg
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | return into THIS frame's caller
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller hanoi (or main) frame's $fp
#
# ------------------------------------------------------------------
#  SNAPSHOT (input: hanoi 3; descended to hanoi(1, A, C, B), about to
#            print its move.  Three frames are live; $fp = innermost.)
#
#      stack, higher addresses at top        (one 24-byte frame per call)
#     +------------------------------+  hanoi(3, A, C, B):
#     | -12 n=3  -16 src=A           |
#     | -20 dst=C  -24 tmp=B  ...    |
#     +------------------------------+  hanoi(2, A, B, C):
#     | -12 n=2  -16 src=A           |
#     | -20 dst=B  -24 tmp=C  ...    |
#     +------------------------------+  hanoi(1, A, C, B):  <-- $fp here
#     | -4  saved $ra (-> hanoi 2)   |
#     | -8  saved $fp (-> hanoi 2)   |
#     | -12 n   = 1                  |
#     | -16 src = 'A'                |
#     | -20 dst = 'C'                |
#     | -24 tmp = 'B'                |
#     +------------------------------+
#           $sp = $fp - 24
#
#   hanoi(1,...) calls hanoi(0,...) (base case, returns at once),
#   prints "Move disk from A to C", then calls hanoi(0,...) again.
#   The full N=3 run prints 7 moves.
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args: 3
#  stdout:
#  | Move disk from A to C
#  | Move disk from A to B
#  | Move disk from C to B
#  | Move disk from A to C
#  | Move disk from B to A
#  | Move disk from B to C
#  | Move disk from A to C

        .data
moveMsg:    .asciiz "Move disk from "
toMsg:      .asciiz " to "
usageMsg:   .asciiz "usage: hanoi N\n"

        .text
        .globl main
main:
        # ---- prologue: build main's 16-byte stack frame ------------
        addiu $sp, $sp, -16
        sw $ra, 12($sp)              # save $ra          -> -4($fp)
        sw $fp, 8($sp)               # save caller's $fp -> -8($fp)
        addiu $fp, $sp, 16

        # if (argc != 2) usage;   (argc arrives in $a0)
        li $t0, 2
        bne $a0, $t0, usage

        # n = atoi(argv[1]);      (argv arrives in $a1)
        lw $a0, 4($a1)
        jal atoi
        sw $v0, -12($fp)             # n = atoi(argv[1])

        # hanoi(n, 'A', 'C', 'B');
        lw $a0, -12($fp)             # $a0 = n
        li $a1, 'A'
        li $a2, 'C'
        li $a3, 'B'
        jal hanoi

        # ---- epilogue ----------------------------------------------
        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 16
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall


# ---------- void hanoi(int n, char src, char dst, char tmp) ----------
# Input:  $a0=n, $a1=src, $a2=dst, $a3=tmp.  Recursive: each call owns
# a 24-byte frame holding $ra, $fp, and all four args.
hanoi:
        beqz $a0, h_done             # base case: n == 0 -> return

        # ---- prologue: build this call's 24-byte frame -------------
        addiu $sp, $sp, -24
        sw $ra, 20($sp)              # save $ra  -> -4($fp)
        sw $fp, 16($sp)              # save $fp  -> -8($fp)
        addiu $fp, $sp, 24
        sw $a0, -12($fp)             # n
        sw $a1, -16($fp)             # src
        sw $a2, -20($fp)             # dst
        sw $a3, -24($fp)             # tmp

        # hanoi(n - 1, src, tmp, dst);
        lw $a0, -12($fp)             # n
        addi $a0, $a0, -1            # n - 1
        lw $a1, -16($fp)             # src
        lw $a2, -24($fp)             # tmp  (becomes the destination)
        lw $a3, -20($fp)             # dst  (becomes the spare)
        jal hanoi

        # print "Move disk from <src> to <dst>\n";
        li $v0, 4
        la $a0, moveMsg
        syscall
        lw $a0, -16($fp)             # src
        li $v0, 11
        syscall
        li $v0, 4
        la $a0, toMsg
        syscall
        lw $a0, -20($fp)             # dst
        li $v0, 11
        syscall
        li $a0, '\n'
        li $v0, 11
        syscall

        # hanoi(n - 1, tmp, dst, src);
        lw $a0, -12($fp)             # n
        addi $a0, $a0, -1            # n - 1
        lw $a1, -24($fp)             # tmp  (becomes the source)
        lw $a2, -20($fp)             # dst
        lw $a3, -16($fp)             # src  (becomes the spare)
        jal hanoi

        # ---- epilogue ----------------------------------------------
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 24

h_done:
        jr $ra


# ---------- atoi subroutine -----------------------------------------
# Shared leaf helper (string -> signed int), kept register-based.
#
#  SYMBOL TABLE (routine: atoi — leaf helper, register-based)
#
#  C variable | memory addr | register | C type | bytes | role / notes
#  -----------+-------------+----------+--------+-------+------------------------------
#  (arg) str  | —           | $a0      | char * |   4   | address of the string, walked
#  value      | —           | $v0      | int    |   4   | accumulator; becomes the return
#  sign       | —           | $t1      | int    |   4   | +1 or -1
#  digit      | —           | $t0      | int    |   4   | one byte decoded to 0..9
#  ten        | —           | $t2      | int    |   4   | constant multiplier
atoi:
        li $v0, 0
        li $t1, 1                    # sign = +1
        lb $t0, ($a0)
        bne $t0, '-', atoi_loop
        li $t1, -1
        addi $a0, $a0, 1
atoi_loop:
        lb $t0, ($a0)
        blt $t0, '0', atoi_done
        bgt $t0, '9', atoi_done
        addi $t0, $t0, -48
        li $t2, 10
        mult $v0, $t2
        mflo $v0
        add $v0, $v0, $t0
        addi $a0, $a0, 1
        j atoi_loop
atoi_done:
        mult $v0, $t1
        mflo $v0
        jr $ra
