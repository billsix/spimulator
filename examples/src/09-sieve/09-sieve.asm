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


# C source — see 09-sieve.c
#
#     static unsigned char sieve[101];   // zero-initialised
#
#     for (int i = 2; i*i <= 100; i++)
#       if (!sieve[i])
#         for (int j = i*i; j <= 100; j += i)
#           sieve[j] = 1;
#
#     for (int i = 2; i <= 100; i++)
#       if (!sieve[i]) { print_int(i); print_char(' '); }
#     print_char('\n');


#PURPOSE:  Sieve of Eratosthenes — print primes up to 100.
#          Eighth demo from PLAN-cs-demos.md.  Two new asm ideas:
#
#            - `.space N` for a BSS-style zero-initialised
#              working buffer (vs `.word ...` for initialised
#              data).  Same directive 12-rev / 18-cksum used for
#              their scratch buffers; here the buffer IS the
#              algorithm's state, not just temporary scratch.
#
#            - **byte-granularity access via `lb`/`sb`**.  Up to
#              now array accesses were 32-bit words via
#              `lw`/`sw` with a `sll by 2` to compute the
#              byte offset from the index.  Here `sieve[i]` is
#              just `base + i` — no shift, because each cell is
#              one byte.
#
#NOTES:    Why mark from i*i (not 2*i):  any smaller multiple
#          of i must have a smaller prime factor (e.g. 2*i is
#          divisible by 2), and that smaller prime already
#          marked it.  Starting at i*i saves doing the work
#          twice.
#
#          Why stop the outer loop at i*i > 100:  any composite
#          ≤ 100 has at least one prime factor ≤ √100 = 10.
#          Once we've sieved with all primes up to 10, every
#          remaining unmarked cell is itself prime.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     i             $s1                  (outer-loop counter, reused
#                                          for both the mark phase
#                                          and the print phase)
#     j             $s2                  (inner-loop counter; mark
#                                          phase only)
#     sieve base    $s3                  (`la sieve`, held across the
#                                          whole demo so we don't
#                                          recompute it each iteration)
#     LIMIT (=100)  $s4                  (held across both phases)
#     sieve         `sieve` (.data)      (101 zero-initialised bytes
#                                          allocated by `.space 101`)
#     i*i           $t0                  (transient; computed once
#                                          per mark-phase outer iter)
#     &sieve[i]/[j] $t1                  (transient address: base + index)
#     sieve[i]/[j]  $t2                  (transient byte / the constant
#                                          1 we store)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra        (saved on entry; restored before
#                                    the final `jr $ra`.  No `jal`
#                                    in this demo — uniform pattern.)
#
#   Volatile:
#     $a0   syscall arg
#     $v0   syscall selector (1 = print_int, 11 = print_char)

        .data
sieve:  .space 101                   # 101 bytes, zero-initialised (BSS)

        .text
        .globl main
main:
        move $s0, $ra

        la $s3, sieve                # base of sieve (held throughout)
        li $s4, 100                  # LIMIT (held throughout)

        # ---- Mark composites ----
        li $s1, 2                    # i = 2
mark_outer:
        mult $s1, $s1
        mflo $t0                     # i*i
        bgt $t0, $s4, after_mark

        # if (!sieve[i])
        add $t1, $s3, $s1            # &sieve[i]
        lbu $t2, ($t1)
        bnez $t2, mark_next_i        # already composite -> skip

        move $s2, $t0                # j = i*i

mark_inner:
        bgt $s2, $s4, mark_next_i    # while (j <= 100)
        add $t1, $s3, $s2            # &sieve[j]
        li $t2, 1
        sb $t2, ($t1)                # sieve[j] = 1
        add $s2, $s2, $s1            # j += i
        j mark_inner

mark_next_i:
        addi $s1, $s1, 1
        j mark_outer

after_mark:
        # ---- Print survivors ----
        li $s1, 2
print_outer:
        bgt $s1, $s4, print_done

        add $t1, $s3, $s1            # &sieve[i]
        lbu $t2, ($t1)
        bnez $t2, print_next_i       # composite -> skip

        move $a0, $s1
        li $v0, 1                    # print_int
        syscall

        li $a0, ' '
        li $v0, 11                   # print_char
        syscall

print_next_i:
        addi $s1, $s1, 1
        j print_outer

print_done:
        li $a0, '\n'
        li $v0, 11
        syscall

        move $ra, $s0
        jr $ra
