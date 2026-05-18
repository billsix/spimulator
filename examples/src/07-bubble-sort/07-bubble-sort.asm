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


# C source — see 07-bubble-sort.c
#
#     static int data[10] = {9, 4, 1, 7, 6, 2, 8, 3, 5, 0};
#
#     void bubble_sort(int *a, int n) {
#       for (int i = 0; i < n - 1; i++)
#         for (int j = 0; j < n - 1 - i; j++)
#           if (a[j] > a[j+1]) {
#             int t = a[j]; a[j] = a[j+1]; a[j+1] = t;
#           }
#     }
#
#     void print_array(int *a, int n) {
#       for (int i = 0; i < n; i++) { print_int(a[i]); print_char(' '); }
#       print_char('\n');
#     }
#
#     void _start(void) {
#       print_string("before: "); print_array(data, 10);
#       bubble_sort(data, 10);
#       print_string("after:  "); print_array(data, 10);
#       os_exit(0);
#     }


#PURPOSE:  Bubble sort a 10-int array in place, print before
#          and after.  Fourth demo from PLAN-cs-demos.md.
#          Introduces:
#            - nested loops (first `for inside for`)
#            - in-place mutation of a `.word`-initialised array
#            - the swap-via-temp pattern
#
#NOTES:    `lw $t6, 4($t4)` is the small but pedagogically nice
#          part of the swap: once we have `&a[j]` in $t4, we can
#          read a[j+1] as a 4-byte offset rather than computing
#          `&a[j+1]` separately.  Same trick on the store side
#          (`sw $t5, 4($t4)`).
#
#          bubble_sort calls nothing.  No need to save $ra
#          inside it — the runtime's $ra arrives in $ra, sits
#          there untouched, and we `jr $ra` to return.
#
#          print_array also calls nothing (only syscalls).  Same
#          situation.
#
#          main DOES `jal` three times, so main saves the
#          runtime's $ra in $s0 and restores at the end.  Same
#          pattern as the previous CS demos.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     data           `data` (.data)      (10 .word cells; mutated
#                                          in place by bubble_sort)
#     beforeMsg, afterMsg                  `.data` strings
#
#   In bubble_sort (no frame, no jal):
#     base (=a)      $a0                  (input arg, address of a)
#     n              $a1                  (input arg, array length)
#     i (outer)      $t0                  (0..n-2)
#     n - 1          $t1                  (computed once at function
#                                           entry, reused as the
#                                           outer-loop bound)
#     j (inner)      $t2                  (0..n-2-i)
#     n - 1 - i      $t3                  (inner-loop upper bound,
#                                           recomputed each outer iter)
#     &a[j]          $t4                  (transient: a + j*4)
#     a[j]           $t5                  (transient: loaded element)
#     a[j+1]         $t6                  (transient: read as 4($t4))
#
#   In print_array (no frame, no jal):
#     base (=a)      $t0                  (saved from $a0 so syscalls
#                                           can reuse $a0)
#     n              $t1                  (saved from $a1)
#     i              $t2                  (0..n-1)
#     &a[i]          $t3                  (transient: a + i*4)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra      (across THREE jals: print_array,
#                                  bubble_sort, print_array again;
#                                  restored before the final
#                                  `jr $ra`)
#
#   Volatile:
#     $a0, $a1   syscall args / function args
#     $v0        syscall selector
#                  (1 = print_int, 4 = print_string, 11 = print_char)

        .data
data:       .word 9, 4, 1, 7, 6, 2, 8, 3, 5, 0
beforeMsg:  .asciiz "before: "
afterMsg:   .asciiz "after:  "

        .text
        .globl main
main:
        move $s0, $ra

        # print_string("before: ")
        li $v0, 4
        la $a0, beforeMsg
        syscall

        # print_array(data, 10)
        la $a0, data
        li $a1, 10
        jal print_array

        # bubble_sort(data, 10)
        la $a0, data
        li $a1, 10
        jal bubble_sort

        # print_string("after:  ")
        li $v0, 4
        la $a0, afterMsg
        syscall

        # print_array(data, 10)
        la $a0, data
        li $a1, 10
        jal print_array

        move $ra, $s0
        jr $ra


# ---------- void bubble_sort(int *a, int n) --------------------------
# In-place ascending sort by adjacent-swap.
# Input:  $a0 = base, $a1 = n
# Trashes: $t0..$t6
bubble_sort:
        addi $t1, $a1, -1            # n - 1  (outer-loop bound)
        li $t0, 0                    # i = 0

bs_outer:
        bge $t0, $t1, bs_done        # while (i < n - 1)

        sub $t3, $t1, $t0            # n - 1 - i  (inner-loop bound)
        li $t2, 0                    # j = 0

bs_inner:
        bge $t2, $t3, bs_next_outer  # while (j < n - 1 - i)

        # Load a[j] and a[j+1] -- the latter via 4($t4) instead of
        # recomputing the address.
        sll $t4, $t2, 2              # j * 4
        add $t4, $a0, $t4            # &a[j]
        lw $t5, 0($t4)               # $t5 = a[j]
        lw $t6, 4($t4)               # $t6 = a[j+1]

        # if (a[j] > a[j+1]) swap
        ble $t5, $t6, bs_no_swap
        sw $t6, 0($t4)               # a[j]   = (old a[j+1])
        sw $t5, 4($t4)               # a[j+1] = (old a[j])

bs_no_swap:
        addi $t2, $t2, 1             # j++
        j bs_inner

bs_next_outer:
        addi $t0, $t0, 1             # i++
        j bs_outer

bs_done:
        jr $ra


# ---------- void print_array(int *a, int n) --------------------------
# Print N decimal ints separated by spaces, then '\n'.
# Input:  $a0 = base, $a1 = n
# Trashes: $a0, $t0..$t3, $v0
print_array:
        move $t0, $a0                # save base
        move $t1, $a1                # save n
        li $t2, 0                    # i = 0

pa_loop:
        bge $t2, $t1, pa_done

        # print_int(a[i])
        sll $t3, $t2, 2              # i * 4
        add $t3, $t0, $t3            # &a[i]
        lw $a0, ($t3)
        li $v0, 1
        syscall

        # print_char(' ')
        li $a0, ' '
        li $v0, 11
        syscall

        addi $t2, $t2, 1
        j pa_loop

pa_done:
        # print_char('\n')
        li $a0, '\n'
        li $v0, 11
        syscall
        jr $ra
