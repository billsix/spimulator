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


# C source — see 28-hanoi.c
#
#     static void hanoi(int n, char src, char dst, char tmp) {
#       if (n == 0) return;
#       hanoi(n - 1, src, tmp, dst);
#       print_string("Move disk from ");
#       print_char(src); print_string(" to "); print_char(dst);
#       print_char('\n');
#       hanoi(n - 1, tmp, dst, src);
#     }
#
#     int my_main(int argc, char **argv) {
#       int n = parse_int(argv[1]);
#       hanoi(n, 'A', 'C', 'B');
#       return 0;
#     }
#
# Invocation:
#   spimulator -f 28-hanoi.asm 3


#PURPOSE:  Print the move sequence that solves Towers of Hanoi
#          for a tower of N disks: A -> C using B as auxiliary.
#          Sixth demo from PLAN-cs-demos.md.  Deepest stack-frame
#          demo so far — each recursive entry pushes FOUR
#          arguments plus $ra onto a 20-byte frame.
#
#          27-fibonacci introduced the stack-frame-per-call
#          pattern with one saved value plus $ra (and an
#          intermediate); this demo extends the pattern to four
#          saved values.  Future demos with even more state per
#          frame would just grow the frame — the discipline is
#          the same.
#
#NOTES:    Why all four args are saved:
#
#            - src AND dst are needed for the print step between
#              the two recursive calls (the print line references
#              both).
#            - All four values (n, src, dst, tmp) are needed to
#              compute the arg vector for the SECOND recursive
#              call: hanoi(n-1, tmp, dst, src).  The first
#              recursive call has long since clobbered $a0..$a3,
#              so we reload everything from the frame.
#
#          Recursion depth equals N.  Output length is 2^N - 1
#          moves; classroom-friendly values are N=3 (7 moves)
#          through N=5 (31 moves).


#STORAGE LAYOUT
#
#   Per-call 20-byte stack frame for hanoi.  Allocated AFTER the
#   n==0 base case test, so leaf calls return without touching
#   the stack.
#
#         higher addresses
#           +-------------+
#           | tmp         |  16($sp)
#           | dst         |  12($sp)
#           | src         |   8($sp)
#           | n           |   4($sp)
#    $sp -> | saved $ra   |   0($sp)
#           +-------------+
#         lower addresses
#
#   main and atoi use the same $s*-only pattern as the other
#   argv-using demos; no per-call frames in those.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     argc          $a0 at entry only
#     argv          $a1 at entry only
#     n             $s1                  (parsed from argv[1])
#     usageMsg                                .data
#
#   In hanoi (per-call 20-byte frame; recursive):
#     n             $a0                  (input arg)
#     src           $a1                  (input arg)
#     dst           $a2                  (input arg)
#     tmp           $a3                  (input arg)
#     saved $ra     0($sp)               (so each frame returns
#                                          to ITS caller, which is
#                                          another hanoi frame one
#                                          level up — or main, at
#                                          the outermost call)
#     saved n       4($sp)               (needed to compute n-1 for
#                                          the second recursive call)
#     saved src     8($sp)               (needed for the print AND
#                                          for the second call)
#     saved dst     12($sp)              (needed for the print AND
#                                          for the second call)
#     saved tmp     16($sp)              (needed for the second call)
#
#   In atoi (same shape as 20-factorial / 21-gcd / 27-fibonacci):
#     value         $v0
#     sign          $t1
#     digit         $t0
#     ten           $t2
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra        (across `jal atoi`, `jal hanoi`;
#                                    restored before the final `jr $ra`)
#     $s1   <- n                    (parsed once; passed to hanoi)
#
#   Per-frame saves (stack-resident; the multi-arg extension of
#   the 27-fibonacci pattern):
#     hanoi's frame holds $ra and ALL four input args.  $s*
#     wouldn't work because each recursive call would overwrite
#     them.  Each frame is independent.
#
#   Volatile:
#     $a0..$a3   function args
#     $v0        syscall selector / atoi return value

        .data
moveMsg:    .asciiz "Move disk from "
toMsg:      .asciiz " to "
usageMsg:   .asciiz "usage: hanoi N\n"

        .text
        .globl main
main:
        move $s0, $ra

        # if (argc != 2) usage
        li $t0, 2
        bne $a0, $t0, usage

        # n = atoi(argv[1])
        lw $a0, 4($a1)
        jal atoi
        move $s1, $v0                # save N

        # hanoi(n, 'A', 'C', 'B')
        move $a0, $s1
        li $a1, 'A'
        li $a2, 'C'
        li $a3, 'B'
        jal hanoi

        move $ra, $s0
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall


# ---------- void hanoi(int n, char src, char dst, char tmp) ----------
# Input:  $a0=n, $a1=src, $a2=dst, $a3=tmp
# Frame:  20 bytes -- see #STORAGE LAYOUT above
hanoi:
        beqz $a0, h_done             # base case: n == 0 -> return

        # Allocate frame and save everything.
        addi $sp, $sp, -20
        sw $ra, 0($sp)
        sw $a0, 4($sp)               # n
        sw $a1, 8($sp)               # src
        sw $a2, 12($sp)              # dst
        sw $a3, 16($sp)              # tmp

        # ---- hanoi(n-1, src, tmp, dst); ----
        # New arg vector: ($a0-1, $a1, $a3, $a2).
        # Only $a2 and $a3 need to swap; $a1 stays, $a0 decrements.
        addi $a0, $a0, -1            # n - 1
        move $t0, $a2                # stash old dst
        move $a2, $a3                # new $a2 = old $a3 (tmp)
        move $a3, $t0                # new $a3 = old $a2 (dst)
        jal hanoi

        # ---- print "Move disk from <src> to <dst>\n" ----
        li $v0, 4
        la $a0, moveMsg
        syscall

        lw $a0, 8($sp)               # src
        li $v0, 11
        syscall

        li $v0, 4
        la $a0, toMsg
        syscall

        lw $a0, 12($sp)              # dst
        li $v0, 11
        syscall

        li $a0, '\n'
        li $v0, 11
        syscall

        # ---- hanoi(n-1, tmp, dst, src); ----
        # Reload everything from the frame; the print syscalls and
        # the first recursive call clobbered $a0..$a3.
        lw $a0, 4($sp)               # n
        addi $a0, $a0, -1            # n - 1
        lw $a1, 16($sp)              # tmp -> 1st arg position (src slot)
        lw $a2, 12($sp)              # dst stays in 2nd arg position
        lw $a3, 8($sp)               # src -> 4th arg position (tmp slot)
        jal hanoi

        # Tear down frame and return.
        lw $ra, 0($sp)
        addi $sp, $sp, 20

h_done:
        jr $ra


# ---------- atoi subroutine -----------------------------------------
# Same shape as the other argv-using demos.
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
