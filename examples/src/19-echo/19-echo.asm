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


# C source — see 19-echo.c
#
#     int my_main(int argc, char **argv) {
#       for (int i = 1; i < argc; i++) {
#         print_string(argv[i]);
#         if (i + 1 < argc) print_char(' ');
#       }
#       print_char('\n');
#       return 0;
#     }
#
# Invocation:
#   spimulator -f 19-echo.asm one two three


#PURPOSE:  Echo command-line arguments to stdout, space-separated,
#          terminated by a newline.  First /examples demo to use
#          argv.  Demonstrates that on entry to `main`, spim has
#          already placed argc in $a0 and argv in $a1 — no syscall
#          needed; just iterate.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     argc          $s1                  (parked on entry from $a0)
#     argv          $s2                  (parked on entry from $a1)
#     i             $s3                  (1..argc-1; loop index walking
#                                          argv[])
#     &argv[i]      $t0                  (transient — argv + (i<<2) on
#                                          each iteration)
#     spaceStr      `spaceStr` (.data)
#     newlineStr    `newlineStr` (.data)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra      (saved on entry; restored before the
#                                  final `jr $ra`.  No `jal` in this
#                                  demo, but the pattern matches what
#                                  18-cksum / 20-factorial need to do
#                                  for actual jal survival.)
#
#     argc / argv / i are also held in $s* registers even though
#     this demo has no `jal`.  In a longer demo (e.g. 21-gcd) the
#     same $s-parking actually does load-bearing work; here it's
#     pre-emptive consistency.
#
#   Volatile (no preserved meaning across a syscall, by convention):
#     $a0   syscall arg
#     $v0   syscall selector (4 = print_string, 11 = print_char)

        .data
spaceStr:   .asciiz " "
newlineStr: .asciiz "\n"

        .text
        .globl main
main:
        # Park entry state in callee-save registers.
        move $s0, $ra
        move $s1, $a0                # argc
        move $s2, $a1                # argv
        li $s3, 1                    # i = 1   (skip argv[0])

loop:
        bge $s3, $s1, done           # while (i < argc)

        # print_string(argv[i]);
        sll $t0, $s3, 2              # i * 4   (each argv pointer is 4 bytes)
        add $t0, $s2, $t0            # &argv[i]
        lw $a0, ($t0)                # argv[i]
        li $v0, 4
        syscall

        # if (i + 1 < argc) print_char(' ');
        addi $t0, $s3, 1
        bge $t0, $s1, no_space
        li $v0, 4
        la $a0, spaceStr
        syscall

no_space:
        addi $s3, $s3, 1             # i++
        j loop

done:
        # print_char('\n');
        li $v0, 4
        la $a0, newlineStr
        syscall

        move $ra, $s0
        li $v0, 0                    # exit status: __start passes this through syscall 17
        jr $ra
