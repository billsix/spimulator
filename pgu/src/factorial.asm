# Copyright 2002 Jonathan Bartlett
# Copyright 2026 William Emerison Six (MIPS/spimulator port)
#
# Permission is granted to copy, distribute and/or modify this program
# under the terms of the GNU Free Documentation License, Version 1.1 or
# any later version published by the Free Software Foundation; with no
# Invariant Sections, with no Front-Cover Texts, and with no Back-Cover
# Texts.  This program is an example from "Programming from the Ground
# Up"; a copy of the license is in the book's GNU FDL appendix (fdlap).


# C source equivalent:
#
#     int factorial(int n) {
#       if (n <= 1) return 1;            // base case
#       return n * factorial(n - 1);     // recursive case
#     }
#
#     int main(void) {
#       return factorial(5);             // 120, seen by the shell as $?
#     }
#
# Invocation:  spimulator -f factorial.asm ; echo $?       # -> 120
#
#
#PURPOSE:  Compute a factorial *recursively* — a function that calls
#          itself.  This is PGU's "factorial" program, retargeted to
#          MIPS/spimulator.  Where power.asm was a single loop, this
#          shows what changes when a function must call others (here,
#          itself): each call needs its own stack frame, and $ra and
#          the live argument must be saved across the recursive `jal`.
#
#
# Why a stack frame is mandatory here (but not in power.asm)
# ==========================================================
# power.asm called nothing, so it could leave $ra alone.  factorial
# calls itself with `jal`, which overwrites $ra every time.  Before
# recursing, each invocation must save:
#   - $ra   (its OWN return address, about to be clobbered by jal), and
#   - n     ($a0, which the recursive call also clobbers, but which we
#           still need afterward to compute n * (n-1)!).
# Each invocation saves them in its own freshly-allocated frame, so the
# saves nest just like the calls do.  This is the whole point of a
# stack: a private scratch area per call.
#
#
#SYMBOL TABLE  (C -> MIPS location)
#   n             $a0          the argument; also reloaded after recursing
#   (return)      $v0          n! handed back to the caller
#   saved $ra     4($sp)       this call's return address
#   saved n       0($sp)       this call's n, needed after the recursive call


        .text
        .globl main
main:
        li   $a0, 5              # factorial(5)
        jal  factorial           # $v0 = 120
        move $a0, $v0            # exit status = result
        li   $v0, 17
        syscall                  # exit 120  (main itself never returns,
                                 #  so it needs no frame and no $ra save)


# ---------- factorial(n=$a0) -> $v0 --------------------------------
# doc-region-begin factorial
factorial:
        # base case: if (n <= 1) return 1;
        li   $t0, 1
        bgt  $a0, $t0, recurse
        li   $v0, 1
        jr   $ra

recurse:
        # open a frame and save what the recursive call would destroy
        addi $sp, $sp, -8
        sw   $ra, 4($sp)         # our return address
        sw   $a0, 0($sp)         # our n

        addi $a0, $a0, -1        # argument for the recursive call: n - 1
        jal  factorial           # $v0 = (n-1)!     (clobbers $ra and $a0)

        lw   $a0, 0($sp)         # restore our n
        lw   $ra, 4($sp)         # restore our return address
        addi $sp, $sp, 8         # close the frame

        mult $v0, $a0            # n! = (n-1)! * n
        mflo $v0
        jr   $ra
# doc-region-end factorial
