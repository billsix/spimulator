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


# C source — see 26-fibonacci-1.c
#
#     static int fib_iter(int n) {
#       int a = 0, b = 1;
#       for (int i = 0; i < n; i++) { int t = a + b; a = b; b = t; }
#       return a;
#     }
#
#     static int fib_rec(int n) {
#       if (n < 2) return n;
#       return fib_rec(n - 1) + fib_rec(n - 2);
#     }
#
#     int my_main(int argc, char **argv) {
#       int n = parse_int(argv[1]);
#       print_string("iter: "); print_int(fib_iter(n)); print_char('\n');
#       print_string("rec:  "); print_int(fib_rec(n));  print_char('\n');
#       return 0;
#     }
#
# Invocation:
#   spimulator -f 26-fibonacci.asm 10


#PURPOSE:  Fibonacci(N) computed two ways from the same N.
#          Second demo from PLAN-cs-demos.md and the FIRST demo
#          to require a per-call stack frame for recursion.
#
#          Earlier demos (07, 18, 20, 22, 23, 24) saved $ra and
#          a couple of locals in callee-save registers
#          ($s0..$s7) and relied on those surviving across a
#          single jal.  That works as long as the demo never
#          re-enters the same function — but fib_rec calls
#          itself, and the second invocation would overwrite the
#          same $s register the first invocation set up.  So
#          this demo introduces the standard MIPS pattern: each
#          recursive entry allocates its own stack frame holding
#          $ra and the live arguments, and releases it on exit.
#
#NOTES:    Overflow.  fib(46) doesn't fit in a signed 32-bit int.
#          print_int will render the result as negative for any
#          N >= 46.  Both implementations overflow identically
#          since the overflow comes from the int type, not from
#          the algorithm.
#
#          Recursion depth.  fib_rec(N) needs N+1 frames deep at
#          worst.  Spim's default stack is 64 KiB; at 12 bytes
#          per frame we can recurse ~5000 levels before
#          exhausting it.  N up to ~40 is fine in practice (the
#          time it takes blows up exponentially long before the
#          stack runs out).
#
#          Iterative time: O(N).
#          Recursive time: O(phi^N) -- fib_rec(30) is already
#          ~1.3 million calls.  Try `time spimulator -f
#          26-fibonacci.asm 30` to feel the difference.


#STORAGE LAYOUT
#
#   fib_iter needs no stack frame at all -- it never calls
#   anything, so it can keep all of (a, b, t, i) in $t* registers.
#
#   fib_rec is where the new pattern shows up.  On EACH recursive
#   entry, fib_rec allocates a 12-byte frame:
#
#         higher addresses
#           +---------------+
#           | fib(n-1)      |   8($sp)   intermediate; saved BETWEEN
#           |               |            the two recursive calls so
#           |               |            the addition at the bottom
#           |               |            can find it
#           | saved n       |   4($sp)   needed for the 2nd call --
#           |               |            the 1st call clobbered $a0
#    $sp -> | saved $ra     |   0($sp)   so we can return to the
#           |               |            caller (which might itself
#           |               |            be another fib_rec frame!)
#           +---------------+
#         lower addresses
#
#   With this discipline, recursion of depth N consumes 12*N
#   bytes of stack.  At fib_rec(10) that's 120 bytes peak --
#   trivial.  At fib_rec(40) it's 480 bytes -- still fine.
#
#   main and atoi use the same $s*-only pattern as 20-factorial
#   and 22-gcd; no per-call frames there.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     argc          $a0 at entry only
#     argv          $a1 at entry only
#     n             $s1                  (parsed from argv[1]; held
#                                          across `jal fib_iter` AND
#                                          `jal fib_rec` so we can
#                                          call them with the same N)
#     iterMsg, recMsg, usageMsg   `.data` strings
#
#   In fib_iter (no frame, no jal):
#     n             $a0                  (input arg; never modified)
#     a             $t0                  (running fib(i-1) -> fib(i))
#     b             $t1                  (running fib(i)   -> fib(i+1))
#     t             $t3                  (a + b scratch)
#     i             $t2                  (loop counter 0..n-1)
#     return value  $v0                  (= a at the end)
#
#   In fib_rec (per-call 12-byte frame; recursive):
#     n             $a0                  (input arg; reloaded from
#                                          4($sp) between the two
#                                          recursive calls because
#                                          the first call clobbered $a0)
#     saved $ra     0($sp)               (so each frame returns to
#                                          ITS caller, which may be
#                                          another fib_rec frame
#                                          one level up)
#     saved n       4($sp)               (the second `jal fib_rec`
#                                          needs n - 2; we reload n
#                                          and decrement)
#     fib(n-1)      8($sp)               (intermediate result of the
#                                          first recursive call;
#                                          held across the second
#                                          recursive call)
#     return value  $v0                  (= n at the base case;
#                                          = fib(n-1) + fib(n-2) at
#                                          the recursive case)
#
#   In atoi (same shape as 20-factorial):
#     value         $v0                  (accumulator -> return)
#     sign          $t1                  (+1 or -1)
#     digit         $t0                  (one byte, decoded to 0..9)
#     ten           $t2                  (constant multiplier)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra      (across `jal atoi`, `jal fib_iter`,
#                                  `jal fib_rec`; restored before the
#                                  final `jr $ra`)
#     $s1   <- n                  (parsed once; held across BOTH the
#                                  fib_iter and the fib_rec calls)
#
#   Per-frame saves (stack-resident; the NEW pattern this demo
#   introduces):
#     fib_rec's $ra, n, and fib(n-1) all live on the stack rather
#     than in $s* registers.  $s* registers wouldn't work here
#     because each recursive call would overwrite them.  Each
#     frame is independent of every other frame in the call
#     chain.
#
#   Volatile (no preserved meaning across a syscall, by convention):
#     $a0   syscall arg / function arg
#     $v0   syscall selector / function return value

        .data
iterMsg:    .asciiz "iter: "
recMsg:     .asciiz "rec:  "
usageMsg:   .asciiz "usage: fibonacci N\n"

        .text
        .globl main
main:
        move $s0, $ra

        # if (argc != 2) usage
        li $t0, 2
        bne $a0, $t0, usage

        # n = atoi(argv[1])
        lw $a0, 4($a1)               # argv[1]
        jal atoi
        move $s1, $v0                # save N (held across both calls)

        # print "iter: "
        li $v0, 4
        la $a0, iterMsg
        syscall

        # print_int(fib_iter(N))
        move $a0, $s1
        jal fib_iter
        move $a0, $v0
        li $v0, 1
        syscall

        # print '\n'
        li $v0, 11
        li $a0, '\n'
        syscall

        # print "rec:  "
        li $v0, 4
        la $a0, recMsg
        syscall

        # print_int(fib_rec(N))
        move $a0, $s1
        jal fib_rec
        move $a0, $v0
        li $v0, 1
        syscall

        # print '\n'
        li $v0, 11
        li $a0, '\n'
        syscall

        move $ra, $s0
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall


# ---------- int fib_iter(int n) --------------------------------------
# Input:  $a0 = n
# Output: $v0 = fib(n)
# Trashes: $t0..$t3
# No frame, no jal -- nothing to save.
fib_iter:
        li $t0, 0                    # a = 0
        li $t1, 1                    # b = 1
        li $t2, 0                    # i = 0
fi_loop:
        bge $t2, $a0, fi_done        # while (i < n)
        add $t3, $t0, $t1            # t = a + b
        move $t0, $t1                # a = b
        move $t1, $t3                # b = t
        addi $t2, $t2, 1             # i++
        j fi_loop
fi_done:
        move $v0, $t0                # return a
        jr $ra


# ---------- int fib_rec(int n) ---------------------------------------
# Input:  $a0 = n
# Output: $v0 = fib(n)
# Frame:  12 bytes  -- see #STORAGE LAYOUT above
# Trashes: $t0 + everything caller-save  (callee-save preserved
#          via stack-resident saves of $ra and n)
fib_rec:
        # if (n < 2) return n;
        li $t0, 2
        bge $a0, $t0, fr_recurse
        move $v0, $a0                # base case: return n
        jr $ra

fr_recurse:
        # Allocate a 12-byte frame: 0 = saved $ra,
        #                           4 = saved n,
        #                           8 = fib(n-1) intermediate.
        addi $sp, $sp, -12
        sw $ra, 0($sp)
        sw $a0, 4($sp)               # save n before the first call
                                     # clobbers $a0

        # fib_rec(n - 1)
        addi $a0, $a0, -1
        jal fib_rec
        sw $v0, 8($sp)               # save fib(n-1) for the addition
                                     # at the bottom -- the next jal
                                     # would overwrite $v0

        # fib_rec(n - 2)
        lw $a0, 4($sp)               # reload n
        addi $a0, $a0, -2
        jal fib_rec
        # $v0 now holds fib(n-2)

        # return fib(n-1) + fib(n-2)
        lw $t0, 8($sp)               # $t0 = fib(n-1)
        add $v0, $v0, $t0            # $v0 = fib(n-2) + fib(n-1)

        # Tear down the frame and return.
        lw $ra, 0($sp)
        addi $sp, $sp, 12
        jr $ra


# ---------- atoi subroutine -----------------------------------------
# Same shape as 20-factorial / 22-gcd / 23-head-file.
# Input:  $a0 = address of decimal string
# Output: $v0 = parsed signed int
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
