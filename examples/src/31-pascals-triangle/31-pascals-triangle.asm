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


# C source — see 31-pascals-triangle-1.c
#
#     static int row[11] = {1};
#
#     for (int n = 0; n < 10; n++) {
#       for (int j = n; j > 0; j--) row[j] += row[j-1];
#       for (int j = 0; j <= n; j++) { print_int(row[j]); print_char(' '); }
#       print_char('\n');
#     }


#PURPOSE:  Print the first 10 rows of Pascal's triangle by
#          building each row in place in a single 11-cell .data
#          array.  Seventh demo from PLAN-cs-demos.md.
#
#          The lesson is the **right-to-left update**: each
#          row[j] of the next generation is row[j] + row[j-1] of
#          the current generation.  If you update left-to-right
#          you overwrite row[j-1] before reading it.  Going
#          right-to-left, the values you write don't depend on
#          values you've already written, so the same array
#          doubles as input and output across generations.
#
#NOTES:    `.word 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0` initialises
#          the array to all zeros except row[0] = 1.  Each
#          generation propagates the leading 1 outward by one
#          cell.
#
#          `-4($t1)` after computing `&row[j]` in $t1 gives you
#          `&row[j-1]` for free — same offset trick as
#          28-bubble-sort's `4($t4)`, just going the other
#          direction.  This is one of the small load/store
#          tricks that makes hand-written asm shorter than the
#          C compiler would produce; the compiler computes
#          &row[j-1] from row + (j-1)*4, but the human sees that
#          (j-1)*4 = j*4 - 4 and saves an instruction.


#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     n             $s1                  (outer loop, 0..9)
#     j             $s2                  (inner loops, reused for BOTH
#                                          the right-to-left update AND
#                                          the left-to-right print)
#     row           `row` (.data)        (11 .word cells; initial
#                                          state [1, 0, 0, ..., 0])
#     &row[j]       $t1                  (transient: row + j*4)
#     row[j]        $t2                  (transient: loaded element /
#                                          new value to store)
#     row[j-1]      $t3                  (transient: loaded via
#                                          -4($t1) — see NOTES)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra        (saved on entry; restored before
#                                    the final `jr $ra`.  No `jal`
#                                    in this demo — uniform pattern.)
#
#     $s1 / $s2 are also held across syscalls, but no `jal`
#     happens here, so the cross-call story is the same as the
#     spim-preserves-everything-on-syscall behavior the earlier
#     Tier-3 demos relied on.
#
#   Volatile:
#     $a0   syscall arg
#     $v0   syscall selector (1 = print_int, 11 = print_char)
#     $t0..$t3  scratch (loop bounds + address arithmetic +
#                        element load/store)

        .data
row:    .word 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

        .text
        .globl main
main:
        move $s0, $ra

        li $s1, 0                    # n = 0

outer:
        # while (n < 10)
        li $t0, 10
        bge $s1, $t0, done

        # ---- In-place right-to-left update ----
        # for (j = n; j > 0; j--) row[j] += row[j-1]
        move $s2, $s1                # j = n

inner_update:
        blez $s2, after_update       # while (j > 0)

        sll $t0, $s2, 2              # j * 4
        la $t1, row
        add $t1, $t1, $t0            # &row[j]

        lw $t2, 0($t1)               # row[j]
        lw $t3, -4($t1)              # row[j-1]  (one cell lower)
        add $t2, $t2, $t3            # row[j] + row[j-1]
        sw $t2, 0($t1)               # row[j] = sum

        addi $s2, $s2, -1            # j--
        j inner_update

after_update:
        # ---- Print this row ----
        # for (j = 0; j <= n; j++) print_int(row[j]); print_char(' ');
        li $s2, 0                    # j = 0

print_loop:
        bgt $s2, $s1, after_print    # while (j <= n)

        sll $t0, $s2, 2
        la $t1, row
        add $t1, $t1, $t0            # &row[j]
        lw $a0, 0($t1)               # row[j]
        li $v0, 1                    # print_int
        syscall

        li $a0, ' '
        li $v0, 11                   # print_char
        syscall

        addi $s2, $s2, 1             # j++
        j print_loop

after_print:
        # print '\n'
        li $a0, '\n'
        li $v0, 11
        syscall

        addi $s1, $s1, 1             # n++
        j outer

done:
        move $ra, $s0
        jr $ra
