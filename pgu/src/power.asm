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
#     int power(int base, int exp) {     // exp must be >= 1
#       int result = base;
#       while (exp > 1) { result = result * base; exp--; }
#       return result;
#     }
#
#     int main(void) {
#       int a = power(2, 3);             // 8
#       int b = power(5, 2);             // 25
#       return a + b;                    // 33, seen by the shell as $?
#     }
#
# Invocation:  spimulator -f power.asm ; echo $?       # -> 33
#
#
#PURPOSE:  Illustrate how functions work by computing 2^3 + 5^2.
#          Programming from the Ground Up's "power" program,
#          retargeted from i386 Linux to MIPS on spim.
#
#
# Calling convention (o32, what spim uses)
# ========================================
# x86 PGU passed arguments by PUSHING them on the stack and read them
# back as 8(%ebp), 12(%ebp), ...  MIPS passes the first four integer
# arguments in registers $a0..$a3, and the return value comes back in
# $v0.  No pushing for these small calls.
#
# The one thing you MUST manage by hand is the return address.  `jal`
# (jump-and-link) stashes the return address in $ra.  A second `jal`
# would overwrite it, so a function that calls others — or `main`,
# which is itself called by spim's startup code — has to preserve $ra
# across the calls.  `main` below saves the runtime's $ra in $s0
# (a callee-saved register) so it survives both `jal power`s.
#
# `power` itself calls nothing, so it does not need to save $ra.  It
# DOES allocate a small stack frame to hold its `result` local in
# memory, mirroring the x86 original's -4(%ebp) slot — this is where
# the prologue/epilogue (`addi $sp,...`) story lives.
#
#
#VARIABLES / SYMBOL TABLE  (C -> MIPS location)
#
#   In main:
#     (runtime $ra) $s0          saved so the final `jr $ra` exits cleanly
#     a             $s1          first answer, held LIVE across `jal power`
#   In power:
#     base          $a0          first argument
#     exp           $a1          second argument (counts down to 1)
#     result        0($sp)       the local, stored in the stack frame
#     (return)      $v0          the answer handed back to the caller


        .text
        .globl main
main:
        move $s0, $ra            # preserve runtime's return address

# doc-region-begin power calls
        # a = power(2, 3)
        li   $a0, 2              # first argument:  base
        li   $a1, 3              # second argument: exponent
        jal  power               # $v0 = 2^3 = 8
        move $s1, $v0            # save the first answer across the next call

        # b = power(5, 2)
        li   $a0, 5
        li   $a1, 2
        jal  power               # $v0 = 5^2 = 25

        # return a + b
        add  $a0, $s1, $v0       # 8 + 25 = 33
# doc-region-end power calls
        li   $v0, 17            # exit2: hand a+b to the shell as $?
        syscall


# ---------- power(base=$a0, exp=$a1) -> $v0 -------------------------
# Requires exp >= 1.  Leaf function (calls nothing), but it keeps its
# `result` local in a stack frame to show the prologue/epilogue.
# doc-region-begin power function
power:
        addi $sp, $sp, -8        # prologue: open an 8-byte frame
        sw   $a0, 0($sp)         # result = base   (the local lives at 0($sp))

power_loop:
        li   $t0, 1
        ble  $a1, $t0, power_done    # if exp <= 1 we are done
        lw   $t1, 0($sp)         # load current result   (load/store!)
        mult $t1, $a0            # result * base -> LO
        mflo $t1
        sw   $t1, 0($sp)         # store the updated result back
        addi $a1, $a1, -1        # exp--
        j    power_loop

power_done:
        lw   $v0, 0($sp)         # return value = result
        addi $sp, $sp, 8         # epilogue: close the frame
        jr   $ra
# doc-region-end power function
