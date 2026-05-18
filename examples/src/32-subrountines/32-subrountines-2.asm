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


# C source — see 32-subrountines.c (same C; -2.asm is the idiomatic
# MIPS port of it, while -1.asm shows the longhand stack-based version
# for comparison).
#
#     int mxPlusB(int m, int x, int b) {
#       int result = m * x + b;
#       return result;
#     }
#
#     __attribute__((noreturn)) void _start(void) {
#       int result1 = mxPlusB(1, 2, 3);
#       print_int(result1);  print_string("\n");
#       int result2 = mxPlusB(4, 5, 6);
#       print_int(result2);  print_string("\n");
#       os_exit(0);
#     }


#PURPOSE:  Run `int mxPlusB(int m, int x, int b)` twice from main and
#          print each result on its own line.  Uses the idiomatic
#          MIPS calling convention (register-passed args, `jal`/`jr
#          $ra` for the call/return).  Compare with -1.asm, which
#          spells the same call out longhand on the stack.
#
#STORAGE LAYOUT
#
#   Tiny stack frame in main only.  mxPlusB needs no frame at all
#   (it touches only $a0..$a2 and $v0).
#
#         higher addresses
#           +-------------+
#           | return code |   4($fp)
#$fp,$sp -> | saved $ra   |   0($fp)
#           +-------------+
#         lower addresses
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     saved $ra      0($fp)             the runtime's $ra, parked on the
#                                       stack so the two `jal mxPlusB`s
#                                       can't lose it; reloaded into $ra
#                                       just before the final `jr $ra`
#     return_value   4($fp)             hoisted into $v0 on the return path
#     m   (per call) $a0                set just before each `jal mxPlusB`
#     x   (per call) $a1                  (")
#     b   (per call) $a2                  (")
#     result1        $v0 (transient)    mxPlusB's return; consumed
#                                       immediately by `move $a0, $v0`
#                                       before the print_int call
#     result2        $v0 (transient)    same shape, second call
#
#   In mxPlusB:
#     m              $a0                input arg
#     x              $a1                input arg
#     b              $a2                input arg
#     result         $v0                computed in place; $v0 is also
#                                       the standard return register
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     (none — this demo uses the older "save $ra to the stack" idiom
#      instead of "save $ra in $s0".  See the `sw $ra, 0($fp)` near
#      the top of main; reloaded with `lw $ra, 0($fp)` at the bottom.
#      The $s* idiom shows up from 18-cksum / 20-factorial onward.)
#
#   Volatile working registers:
#     $a0..$a2  function args at the call sites; $a0 also reused as
#               syscall arg between calls
#     $v0       syscall selector / function return value
#     $t0       scratch (the return-code initializer)

        .data
nl:    .asciiz     "\n"

        .text
        .globl main


# ---------- int mxPlusB(int m, int x, int b) ----------
mxPlusB:
        move $fp, $sp                # $fp = $sp (formal frame; not actually used)

        # int result = m * x + b;
        mult $a1, $a0                # hi:lo = x * m  (hidden product registers)
        mflo $v0                     # $v0 = low 32 bits of m*x
        addu $v0, $a2, $v0           # $v0 = b + (m*x)  = m*x + b

        # return result;              -- $v0 is the return register
        jr $ra                       # jump to the address in $ra


# ---------- _start (main) ----------
main:
        # ---- frame setup ----
        move $fp, $sp                # set the frame pointer to the stack pointer
        addi $fp, $fp, -8            # reserve 2 cells (saved $ra + return code)

        # Save $ra — the upcoming `jal`s would otherwise clobber it.
        sw $ra, 0($fp)               # store $ra into the saved-$ra slot

        # return_value = 0;
        li $t0, 0
        sw $t0, 4($fp)               # store $t0 into the return-code slot


        # ---- result1 = mxPlusB(1, 2, 3); ----
        move $sp, $fp                # $sp pinned at $fp (no extra args block)
        li $a0, 1                    # m = 1
        li $a1, 2                    # x = 2
        li $a2, 3                    # b = 3
        jal mxPlusB                  # call mxPlusB; $ra := &continueMainPt1

continueMainPt1:
        move $sp, $fp                # $sp pinned at $fp

        # print_int(result1);         -- $v0 still holds mxPlusB's result
        move $a0, $v0                # arg = result1
        li $v0, 1                    # syscall 1 = print_int
        syscall

        # print_string("\n");
        li $v0, 4                    # syscall 4 = print_string
        la $a0, nl                   # arg = address of "\n"
        syscall


        # ---- result2 = mxPlusB(4, 5, 6); ----
        move $sp, $fp
        li $a0, 4                    # m = 4
        li $a1, 5                    # x = 5
        li $a2, 6                    # b = 6
        jal mxPlusB                  # call mxPlusB; $ra := &continueMainPt2

continueMainPt2:
        move $sp, $fp

        # print_int(result2);
        move $a0, $v0
        li $v0, 1
        syscall

        # print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall


        # return return_value;        -- os_exit(0) in the C source
        lw $ra, 0($fp)               # restore caller's $ra
        lw $v0, 4($fp)               # return code goes in $v0
        addi $fp, $fp, 8             # tear down the frame
        jr $ra                       # jump to the address in $ra
