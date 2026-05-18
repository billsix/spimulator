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


# C source — see 32-subrountines.c
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
#
# This is the "longhand" implementation: every part of the call is
# stored explicitly on the stack — args, the address to write the
# result into, the continuation address, and the caller's $fp.
# `mxPlusB` does NOT use `jal`/`jr $ra`; it loads a continuation
# address from the stack and `jr`s to it.  -2.asm uses the idiomatic
# MIPS register-passing + jal convention.


#PURPOSE:  Run `int mxPlusB(int m, int x, int b)` twice from main and
#          print each result on its own line.  Demonstrates the most
#          explicit possible subroutine-calling convention: every
#          part of the call (args, result address, continuation,
#          saved $fp) is laid out on the stack by hand.
#
#NOTES:    The actual hex values of $sp and $fp shown in any
#          `spimulator -explain` trace are arbitrary — they're
#          whatever the OS happened to assign when it launched the
#          program.  What's meaningful is the relative offsets
#          (0($fp), 4($fp), ..., 20($fp)) — those are the same on
#          every run.
#
#STORAGE LAYOUT
#
#   No callee-save registers in this demo — every named C value
#   lives on the stack.  Working registers ($t0..$t4, $a0, $v0)
#   are volatile.
#
#   Stack at peak (during a mxPlusB call, before mxPlusB adopts
#   the call block as its own frame):
#
#         higher addresses
#           +-------------+
#           | return_val  |   8($fp)    \
#           | result2     |   4($fp)     |  main's locals (3 cells)
#    $fp -> | result1     |   0($fp)    /
#           +-------------+
#           | saved $fp   |  20($sp)    \
#           | continuation|  16($sp)     |  call block laid out
#           | &result_cell|  12($sp)     |  by main; mxPlusB will
#           | b           |   8($sp)     |  `move $fp, $sp` and
#           | x           |   4($sp)     |  read these as
#    $sp -> | m           |   0($sp)    /   0..20($fp)
#           +-------------+
#         lower addresses
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     result1        0($fp)             hoisted into $a0 before its print_int
#     result2        4($fp)             hoisted into $a0 before its print_int
#     return_value   8($fp)             hoisted into $v0 on the return path
#                                       (note the intentional post-teardown
#                                        read — see the in-body NOTE)
#
#   In mxPlusB (callee, after `move $fp, $sp` adopts the call block):
#     m              0($fp)             temporarily in $t0 (m*x+b sequence)
#     x              4($fp)             temporarily in $t1   (")
#     b              8($fp)             temporarily in $t2   (")
#     result         (none — computed in $t4 and stored directly via
#                     &result_cell; never named in memory inside mxPlusB)
#     &result_cell   12($fp)            temporarily in $t0 (just before sw)
#     continuation   16($fp)            temporarily in $t0 (just before jr)
#     saved $fp      20($fp)            restored TO $fp at exit
#
#   Cross-call saves (callee-save $s* values held LIVE across the
#   `j mxPlusB` boundary):
#     (none — this demo uses no $s* registers at all.  Every value
#      that needs to outlive the call is on the stack: the
#      continuation address goes in 16($sp), the caller's $fp goes
#      in 20($sp), and main's locals sit in its own frame above.
#      The $s* / "save into a callee-save register" idiom shows up
#      in 18-cksum and 20-factorial.)
#
#   Volatile working registers:
#     $a0   syscall arg
#     $v0   syscall selector / return value
#     $t0..$t4  scratch (the m*x+b product; also the load-then-use
#                        pattern for every stack slot above)

        .data
nl:    .asciiz     "\n"

        .text
        .globl main


# ---------- int mxPlusB(int m, int x, int b) ----------
mxPlusB:
        # Adopt the caller-laid-out args block as our frame.
        move $fp, $sp                # $fp = $sp

        # int result = m * x + b;
        lw $t0, 0($fp)               # $t0 = m
        lw $t1, 4($fp)               # $t1 = x
        lw $t2, 8($fp)               # $t2 = b
        mult $t0, $t1                # hi:lo = m * x  (hidden product registers)
        mflo $t0                     # $t0 = low 32 bits of m*x
        addu $t4, $t0, $t2           # $t4 = m*x + b

        # *result_cell = result;     -- write into the caller-supplied slot
        lw $t0, 12($fp)              # $t0 = &result_cell
        sw $t4, ($t0)                # store m*x+b at *$t0

        # Restore the caller's $fp and jump to the continuation address.
        lw $t0, 16($fp)              # $t0 = continuation
        lw $fp, 20($fp)              # $fp = caller's $fp
        jr $t0                       # jump to the continuation


# ---------- _start (main) ----------
main:
        # ---- frame setup ----
        move $fp, $sp                # set the frame pointer to the stack pointer
        addi $fp, $fp, -12           # reserve 3 int32_t cells

        # int result1, result2; int return_value = 0; (default-initialise)
        li $t0, 0
        sw $t0, 0($fp)               # result1 = 0
        li $t0, 0
        sw $t0, 4($fp)               # result2 = 0
        li $t0, 0
        sw $t0, 8($fp)               # return_value = 0


        # ---- result1 = mxPlusB(1, 2, 3); ----
        # Push the call block below the frame: $sp = $fp - 24.
        addi $sp, $fp, -24           # carve out 24 bytes for the call block

        # m, x, b
        li $t0, 1
        sw $t0, 0($sp)               # m = 1
        li $t0, 2
        sw $t0, 4($sp)               # x = 2
        li $t0, 3
        sw $t0, 8($sp)               # b = 3

        # &result1 (the cell at 0($fp))
        move $t0, $fp                # $t0 = $fp
        addiu $t0, $t0, 0            # $t0 = $fp + 0 (i.e. &result1)
        sw $t0, 12($sp)              # store &result1 into the call block

        # continuation = address of the next-instruction-after-the-call
        la $t0, continueMainPt1      # $t0 = &continueMainPt1
        sw $t0, 16($sp)              # store continuation into the call block

        # saved $fp = current $fp (so mxPlusB can restore it)
        la $t0, ($fp)                # $t0 = $fp
        sw $t0, 20($sp)              # store saved $fp into the call block

        # Call.  `j` (not `jal`) — the return address was stashed in
        # 16($sp) above instead.
        j mxPlusB

continueMainPt1:
        # mxPlusB has returned: result1 is now in 0($fp).  Pop the
        # call block (just move $sp back to $fp).
        move $sp, $fp                # $sp = $fp

        # print_int(result1);
        lw $a0, 0($fp)               # arg = result1
        li $v0, 1                    # syscall 1 = print_int
        syscall

        # print_string("\n");
        li $v0, 4                    # syscall 4 = print_string
        la $a0, nl                   # arg = address of "\n"
        syscall


        # ---- result2 = mxPlusB(4, 5, 6); ----
        addi $sp, $fp, -24

        # m, x, b
        li $t0, 4
        sw $t0, 0($sp)
        li $t0, 5
        sw $t0, 4($sp)
        li $t0, 6
        sw $t0, 8($sp)

        # &result2 (the cell at 4($fp))
        move $t0, $fp
        addiu $t0, $t0, 4            # $t0 = $fp + 4 (i.e. &result2)
        sw $t0, 12($sp)

        # continuation
        la $t0, continueMainPt2
        sw $t0, 16($sp)

        # saved $fp
        la $t0, ($fp)
        sw $t0, 20($sp)

        j mxPlusB

continueMainPt2:
        move $sp, $fp

        # print_int(result2);
        lw $a0, 4($fp)               # arg = result2
        li $v0, 1
        syscall

        # print_string("\n");
        li $v0, 4
        la $a0, nl
        syscall


        # return return_value;        -- os_exit(0) in the C source
        addi $fp, $fp, 12            # tear down the frame
        # NOTE: the next line reads 4($fp) AFTER tearing down — that's
        # really -8 relative to the original $fp (i.e. the old result2
        # slot, now outside the frame).  Pre-existing oddity in this
        # demo; preserved as-is.
        lw $v0, 4($fp)               # return code goes in $v0
        jr $ra                       # jump to the address in $ra
