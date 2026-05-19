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
#     int my_main(int argc, char **argv) {
#       int limit = (argc == 2) ? parse_int(argv[1]) : 100;
#       unsigned char *sieve = sbrk(limit + 1);   // zero-filled
#       for (int i = 2; i*i <= limit; i++)
#         if (!sieve[i])
#           for (int j = i*i; j <= limit; j += i) sieve[j] = 1;
#       for (int i = 2; i <= limit; i++)
#         if (!sieve[i]) { print_int(i); print_char(' '); }
#       print_char('\n');
#     }


#PURPOSE:  Sieve of Eratosthenes with N from argv.  Default 100.
#          **First curriculum demo to use sbrk (syscall 9)** — the
#          flag array is allocated dynamically because LIMIT is
#          only known at runtime.
#
#NOTES:    Syscall 9 (sbrk) on spim:
#            $a0 = bytes to add to the data segment
#            $v0 = PREVIOUS top (the base of the new region)
#          The kernel zero-fills new pages, so we don't need to
#          memset the flag array to zero ourselves.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     limit         $s1                  (from argv or 100)
#     sieve base    $s3                  (returned by sbrk)
#     i             $s4                  (loop counter, mark + print)
#     j             $s5                  (inner mark loop)
#     i*i           $t0                  (transient outer-bound check)
#     &sieve[i/j]   $t1                  (transient address)
#
#   Cross-call saves:
#     $s0   <- runtime's $ra
#     $s1   <- limit
#     $s3   <- sieve base address (from sbrk)

        .data
usageMsg:   .asciiz "usage: sieve [N]   (N >= 2)\n"

        .text
        .globl main
main:
        move $s0, $ra
        li $s1, 100                  # limit = 100 (default)

        li $t0, 1
        beq $a0, $t0, alloc
        li $t0, 2
        bne $a0, $t0, usage

        lw $a0, 4($a1)
        jal atoi
        move $s1, $v0
        li $t0, 2
        blt $s1, $t0, usage

alloc:
        # sbrk(limit + 1) — returns base of new region in $v0
        addi $a0, $s1, 1
        li $v0, 9                    # 9 = sbrk
        syscall
        move $s3, $v0                # sieve base address

        # ---- Mark composites ----
        li $s4, 2                    # i = 2
mark_outer:
        mult $s4, $s4
        mflo $t0                     # i*i
        bgt $t0, $s1, after_mark

        # if (!sieve[i])
        add $t1, $s3, $s4
        lbu $t2, ($t1)
        bnez $t2, mark_next_i

        move $s5, $t0                # j = i*i

mark_inner:
        bgt $s5, $s1, mark_next_i
        add $t1, $s3, $s5
        li $t2, 1
        sb $t2, ($t1)
        add $s5, $s5, $s4
        j mark_inner

mark_next_i:
        addi $s4, $s4, 1
        j mark_outer

after_mark:
        # ---- Print survivors ----
        li $s4, 2
print_outer:
        bgt $s4, $s1, print_done

        add $t1, $s3, $s4
        lbu $t2, ($t1)
        bnez $t2, print_next_i

        move $a0, $s4
        li $v0, 1
        syscall

        li $a0, ' '
        li $v0, 11
        syscall

print_next_i:
        addi $s4, $s4, 1
        j print_outer

print_done:
        li $a0, '\n'
        li $v0, 11
        syscall

        move $ra, $s0
        li $v0, 0                    # exit status: __start passes this through syscall 17
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall


# ---------- atoi subroutine -----------------------------------------
atoi:
        li $v0, 0
        li $t1, 1
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
