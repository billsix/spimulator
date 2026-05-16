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


# C source — see 27-binary-search-1.c
#
#     static const int data[] = {3, 7, 11, 19, 23, 31, 41, 53, 67, 71};
#
#     static int linear_search(int target) {
#       for (int i = 0; i < 10; i++)
#         if (data[i] == target) return i;
#       return -1;
#     }
#
#     static int binary_search(int target) {
#       int lo = 0, hi = 9;
#       while (lo <= hi) {
#         int mid = (lo + hi) / 2;
#         if (data[mid] == target) return mid;
#         if (data[mid] < target)  lo = mid + 1;
#         else                     hi = mid - 1;
#       }
#       return -1;
#     }
#
# Invocation:
#   spimulator -f 27-binary-search.asm 41
#   spimulator -f 27-binary-search.asm 50    # not found


#PURPOSE:  Linear and binary search over a hardcoded sorted 10-
#          element int array.  Third demo from PLAN-cs-demos.md.
#          Introduces:
#            - `.data` arrays via `.word`  (10 contiguous 32-bit
#              cells under a single label)
#            - strided indexed access      (&data[i] = data + i*4,
#              i.e. `sll $t, $i, 2; add $t, base, $t`)
#            - divide-and-conquer in asm   (mid = (lo+hi)/2 becomes
#              `add` + `srl by 1`, no `div` needed)
#
#NOTES:    The classic binary-search overflow bug (`(lo + hi) / 2`
#          overflowing for very large arrays) doesn't apply here
#          — the array is 10 elements so lo+hi maxes at 18.
#          Worth being aware of when the same pattern appears at
#          industrial scale (the textbook fix is `lo + (hi-lo)/2`).
#
#          Returning -1 to signal "not found" is C convention;
#          main checks `bltz $v0` and prints "not found" instead
#          of the index.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     argc          $a0 at entry only
#     argv          $a1 at entry only
#     target        $s1                  (parsed from argv[1] via atoi;
#                                          held across BOTH
#                                          `jal linear_search` and
#                                          `jal binary_search`)
#     data          `data` (.data)       (10 contiguous .word cells)
#     linMsg, binMsg, nfMsg, usageMsg     `.data` strings
#
#   In linear_search:
#     target        $a0                  (input arg)
#     base          $t0                  (address of data[])
#     i             $t1                  (loop index 0..9)
#     length        $t2                  (constant 10)
#     &data[i]      $t3                  (transient: base + i*4)
#     data[i]       $t4                  (transient: loaded element)
#     return value  $v0                  (= i at match, = -1 at end)
#
#   In binary_search:
#     target        $a0                  (input arg)
#     base          $t0                  (address of data[])
#     lo            $t1                  (search window low bound)
#     hi            $t2                  (search window high bound)
#     mid           $t3                  ((lo+hi)/2, via `srl by 1`)
#     &data[mid]    $t4                  (transient: base + mid*4)
#     data[mid]     $t5                  (transient: loaded element)
#     return value  $v0                  (= mid at match, = -1 at end)
#
#   In atoi (same shape as 20-factorial / 22-gcd / 26-fibonacci):
#     value         $v0                  (accumulator -> return)
#     sign          $t1                  (+1 or -1)
#     digit         $t0                  (one byte, decoded to 0..9)
#     ten           $t2                  (constant multiplier)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra      (across `jal atoi`,
#                                  `jal linear_search`,
#                                  `jal binary_search`; restored
#                                  before the final `jr $ra`)
#     $s1   <- target             (parsed once; held across BOTH
#                                  search calls so we can pass it
#                                  to each)
#
#   Volatile:
#     $a0   syscall arg / function arg
#     $v0   syscall selector / function return value

        .data
data:       .word 3, 7, 11, 19, 23, 31, 41, 53, 67, 71
linMsg:     .asciiz "linear: "
binMsg:     .asciiz "binary: "
nfMsg:      .asciiz "not found"
usageMsg:   .asciiz "usage: binary-search TARGET\n"

        .text
        .globl main
main:
        move $s0, $ra

        # if (argc != 2) usage
        li $t0, 2
        bne $a0, $t0, usage

        # target = atoi(argv[1])
        lw $a0, 4($a1)
        jal atoi
        move $s1, $v0                # save target

        # ---- linear ----
        li $v0, 4
        la $a0, linMsg
        syscall

        move $a0, $s1
        jal linear_search
        jal print_idx_or_nf

        # ---- binary ----
        li $v0, 4
        la $a0, binMsg
        syscall

        move $a0, $s1
        jal binary_search
        jal print_idx_or_nf

        move $ra, $s0
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17                   # exit2(1)
        syscall


# ---------- print_idx_or_nf ------------------------------------------
# If $v0 >= 0 print it as a signed int; else print "not found".
# Then print '\n'.  Trashes $a0, $v0.
#
# This subroutine intentionally lives below main's printing flow
# rather than getting inlined, since the same logic runs after
# each of the two `jal *_search` calls.
print_idx_or_nf:
        bltz $v0, pin_nf
        move $a0, $v0
        li $v0, 1                    # 1 = print_int
        syscall
        j pin_nl
pin_nf:
        li $v0, 4
        la $a0, nfMsg
        syscall
pin_nl:
        li $v0, 11
        li $a0, '\n'
        syscall
        jr $ra


# ---------- int linear_search(int target) ----------------------------
# Walks data[0..9], returns the first matching index or -1.
# Input:  $a0 = target
# Output: $v0 = index, or -1 if not found
# Trashes: $t0..$t4
linear_search:
        la $t0, data
        li $t1, 0                    # i = 0
        li $t2, 10                   # length
ls_loop:
        bge $t1, $t2, ls_notfound
        sll $t3, $t1, 2              # i * 4
        add $t3, $t0, $t3            # &data[i]
        lw $t4, ($t3)
        beq $t4, $a0, ls_found
        addi $t1, $t1, 1
        j ls_loop
ls_found:
        move $v0, $t1
        jr $ra
ls_notfound:
        li $v0, -1
        jr $ra


# ---------- int binary_search(int target) ----------------------------
# Halves the search window each iteration; returns the matching
# index or -1.
# Input:  $a0 = target
# Output: $v0 = index, or -1 if not found
# Trashes: $t0..$t5
binary_search:
        la $t0, data
        li $t1, 0                    # lo = 0
        li $t2, 9                    # hi = DATA_LEN - 1
bs_loop:
        bgt $t1, $t2, bs_notfound    # while (lo <= hi)

        # mid = (lo + hi) / 2     (srl by 1 since both are non-negative)
        add $t3, $t1, $t2
        srl $t3, $t3, 1

        # data[mid]
        sll $t4, $t3, 2              # mid * 4
        add $t4, $t0, $t4            # &data[mid]
        lw $t5, ($t4)

        beq $t5, $a0, bs_found
        bgt $t5, $a0, bs_high        # data[mid] > target  -> shrink right
        addi $t1, $t3, 1             # else lo = mid + 1
        j bs_loop
bs_high:
        addi $t2, $t3, -1            # hi = mid - 1
        j bs_loop
bs_found:
        move $v0, $t3                # return mid
        jr $ra
bs_notfound:
        li $v0, -1
        jr $ra


# ---------- atoi subroutine -----------------------------------------
# Same shape as the previous argv demos.
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
