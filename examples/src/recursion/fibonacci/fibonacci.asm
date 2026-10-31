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


# C source — see fibonacci.c
# Invocation:
#   spimulator -f fibonacci.asm 10


#PURPOSE:  Fibonacci(N) computed two ways from the same N — iterative
#          and recursive — so the reader can compare a flat loop with
#          a recursion that needs a per-call stack frame.
#
#NOTES:    Overflow.  fib(46) doesn't fit in a signed 32-bit int;
#          print_int renders N >= 46 as negative.  Both implementations
#          overflow identically (it comes from the int type).
#
#          Recursion depth.  fib_rec(N) nests N+1 frames deep at worst;
#          at 16 bytes/frame spim's 64 KiB stack is plenty.  The O(phi^N)
#          running time blows up long before the stack does.
#
#          STACK-VISIBLE STYLE: every C local lives on the stack.
#          - main homes `n` in its frame (it is reused for both the
#            fib_iter and fib_rec calls), and saves $ra (it makes jals).
#          - fib_iter keeps its running locals a, b, i in stack slots
#            (the transient `t = a + b` stays in a register); it makes
#            no jal, so it saves $fp but not $ra.
#          - fib_rec is the lesson: because it calls ITSELF, the same
#            register would be overwritten by the inner call, so n, the
#            saved $ra, and the fib(n-1) intermediate all live in THIS
#            call's own frame at fixed -N($fp) offsets.  Each recursive
#            entry gets an independent frame.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  argc        | —           | $a0        | int       |   4   | entry only; tested (== 2), then dead
#  argv        | —           | $a1        | char **   |   4   | entry only; argv[1] fetched for atoi
#  n           | -12($fp)    | $a0 (copy) | int       |   4   | parsed N; passed to both fib_iter and fib_rec
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | jal atoi / fib_iter / fib_rec
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: fib_iter — iterative; no jal)
#
#  C variable  | memory addr | register   | C type | bytes | role / notes
#  ------------+-------------+------------+--------+-------+---------------------------
#  (arg) n     | —           | $a0        | int    |   4   | loop bound; never clobbered (no jal)
#  a           | -8($fp)     | $t0 (copy) | int    |   4   | fib(i-1) -> fib(i)
#  b           | -12($fp)    | $t1 (copy) | int    |   4   | fib(i)   -> fib(i+1)
#  i           | -16($fp)    | $t0 (copy) | int    |   4   | loop counter 0..n-1
#  t           | —           | $t2        | int    |   4   | transient a + b (consumed at once)
#  (saved $fp) | -4($fp)     | —          | frame ptr |  4  | caller's frame pointer
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: fib_rec — recursive; one frame PER call)
#
#  C variable     | memory addr | register   | C type    | bytes | role / notes
#  ---------------+-------------+------------+-----------+-------+------------------------
#  (arg) n        | -12($fp)    | $a0 (copy) | int       |   4   | saved so the 2nd call can reload n-2
#  fib(n-1)       | -16($fp)    | $t0 (copy) | int       |   4   | 1st call's result, held across 2nd call
#  return value   | —           | $v0        | int       |   4   | n (base) or fib(n-1)+fib(n-2)
#  (saved $ra)    | -4($fp)     | —          | code addr |   4   | return into THIS frame's caller
#  (saved $fp)    | -8($fp)     | —          | frame ptr |   4   | caller fib_rec frame's $fp
#
# ------------------------------------------------------------------
#  SNAPSHOT (input: fibonacci 4; the recursion has descended to
#            fib_rec(2), which is about to call fib_rec(1).  Each
#            active call has its OWN frame; $fp points at the
#            innermost one.)
#
#      stack, higher addresses at top        (one 16-byte frame per call)
#     +------------------------------+
#     | main's frame ... n = 4       |
#     +------------------------------+  fib_rec(4):
#     | -4  saved $ra (-> main)      |
#     | -8  saved $fp (-> main)      |
#     | -12 n        = 4             |
#     | -16 fib(n-1) = (pending)     |
#     +------------------------------+  fib_rec(3):
#     | -4  saved $ra (-> fib_rec 4) |
#     | -8  saved $fp (-> fib_rec 4) |
#     | -12 n        = 3             |
#     | -16 fib(n-1) = (pending)     |
#     +------------------------------+  fib_rec(2):  <-- $fp here
#     | -4  saved $ra (-> fib_rec 3) |
#     | -8  saved $fp (-> fib_rec 3) |
#     | -12 n        = 2             |           $a0 = 1 (n-1 for the
#     | -16 fib(n-1) = (pending)     |                   next call)
#     +------------------------------+
#           $sp = $fp - 16
#
#   fib_rec(2) calls fib_rec(1) (base case -> 1), stores it at -16,
#   then calls fib_rec(0) (-> 0); returns 1.  Unwinding gives
#   fib(4) = 3.  Final output: "iter: 3" then "rec:  3".
#
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args: 10
#  stdout:
#  | iter: 55
#  | rec:  55

        .data
iterMsg:    .asciiz "iter: "
recMsg:     .asciiz "rec:  "
usageMsg:   .asciiz "usage: fibonacci N\n"

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
        lw $a0, 4($a1)               # argv[1]
        jal atoi
        sw $v0, -12($fp)             # n = atoi(argv[1])

        # print_string("iter: ");
        li $v0, 4
        la $a0, iterMsg
        syscall

        # print_int(fib_iter(n));
        lw $a0, -12($fp)             # $a0 = n
        jal fib_iter
        move $a0, $v0
        li $v0, 1
        syscall

        # print_char('\n');
        li $v0, 11
        li $a0, '\n'
        syscall

        # print_string("rec:  ");
        li $v0, 4
        la $a0, recMsg
        syscall

        # print_int(fib_rec(n));
        lw $a0, -12($fp)             # $a0 = n (reloaded; unchanged)
        jal fib_rec
        move $a0, $v0
        li $v0, 1
        syscall

        # print_char('\n');
        li $v0, 11
        li $a0, '\n'
        syscall

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


# ---------- int fib_iter(int n) --------------------------------------
# Iterative Fibonacci.  No jal -> saves $fp but not $ra.  a, b, i live
# in the frame; the sum t = a + b is a transient kept in a register.
# Input:  $a0 = n    Output: $v0 = fib(n)
fib_iter:
        addiu $sp, $sp, -16
        sw $fp, 12($sp)              # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 16

        # a = 0; b = 1; i = 0;
        li $t0, 0
        sw $t0, -8($fp)              # a = 0
        li $t0, 1
        sw $t0, -12($fp)             # b = 1
        li $t0, 0
        sw $t0, -16($fp)             # i = 0

fi_loop:
        lw $t0, -16($fp)             # $t0 = i
        bge $t0, $a0, fi_done        # while (i < n)

        # t = a + b;  a = b;  b = t;
        lw $t0, -8($fp)              # $t0 = a
        lw $t1, -12($fp)             # $t1 = b
        add $t2, $t0, $t1            # t = a + b  (transient)
        sw $t1, -8($fp)              # a = b
        sw $t2, -12($fp)             # b = t

        # i++;
        lw $t0, -16($fp)             # $t0 = i
        addi $t0, $t0, 1
        sw $t0, -16($fp)
        j fi_loop

fi_done:
        lw $v0, -8($fp)              # return a
        lw $fp, -4($fp)
        addiu $sp, $sp, 16
        jr $ra


# ---------- int fib_rec(int n) ---------------------------------------
# Recursive Fibonacci.  Calls itself, so every live value lives in
# THIS call's own 16-byte frame: saved $ra, saved $fp, n, and the
# fib(n-1) intermediate held across the second recursive call.
# Input:  $a0 = n    Output: $v0 = fib(n)
fib_rec:
        # if (n < 2) return n;   (base case: no frame needed)
        li $t0, 2
        bge $a0, $t0, fr_recurse
        move $v0, $a0
        jr $ra

fr_recurse:
        addiu $sp, $sp, -16
        sw $ra, 12($sp)              # save $ra          -> -4($fp)
        sw $fp, 8($sp)               # save caller's $fp -> -8($fp)
        addiu $fp, $sp, 16
        sw $a0, -12($fp)             # save n (the 1st call clobbers $a0)

        # fib_rec(n - 1)
        lw $a0, -12($fp)             # $a0 = n
        addi $a0, $a0, -1            # n - 1
        jal fib_rec
        sw $v0, -16($fp)             # fib(n-1)  (held across the 2nd call)

        # fib_rec(n - 2)
        lw $a0, -12($fp)             # reload n
        addi $a0, $a0, -2            # n - 2
        jal fib_rec
        # $v0 = fib(n-2)

        # return fib(n-1) + fib(n-2);
        lw $t0, -16($fp)             # $t0 = fib(n-1)
        add $v0, $v0, $t0            # fib(n-2) + fib(n-1)

        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 16
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
