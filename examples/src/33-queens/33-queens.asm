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


# C source — see 33-queens-1.c
#
#     static int columns[8];
#
#     int safe(int row, int col) {
#       for (int i = 0; i < row; i++) {
#         int c = columns[i];
#         if (c == col) return 0;
#         if (c - col == row - i) return 0;
#         if (col - c == row - i) return 0;
#       }
#       return 1;
#     }
#
#     void solve(int row) {
#       if (row == 8) { print_solution(); return; }
#       for (int col = 0; col < 8; col++) {
#         if (safe(row, col)) {
#           columns[row] = col;
#           solve(row + 1);
#         }
#       }
#     }


#PURPOSE:  Find every solution to the 8-Queens problem via
#          backtracking and print each on its own line as 8
#          column indices.  Ninth and final demo from
#          PLAN-cs-demos.md.
#
#          The shape introduced here is **backtracking**: at
#          each row, try every column; if it's safe, tentatively
#          place the queen and recurse; if the recursive call
#          eventually returns, the column loop just moves on to
#          the next candidate.  There's no explicit "undo" step
#          because the next iteration overwrites columns[row]
#          anyway.
#
#NOTES:    solve() needs a 12-byte per-call frame: saved $ra,
#          saved row, and the col loop counter.  The col counter
#          MUST live on the stack rather than in $s* because each
#          recursive solve() invocation has its own col, and a
#          shared $s* register would get overwritten by the
#          recursive call.  This is the same lesson 26-fibonacci
#          and 30-hanoi taught: per-call state goes in the
#          per-call frame.
#
#          safe() doesn't recurse and calls nothing, so it gets
#          by with no frame at all — pure $t* + $a* + $v0.
#
#          print_solution() likewise has no frame; it only
#          issues syscalls.
#
#          Recursion depth peaks at 8 frames (rows 0..7), which
#          is 96 bytes — well within spim's 64 KiB default stack.
#          The expected output is exactly 92 lines.


#STORAGE LAYOUT
#
#   Per-call 12-byte stack frame for solve().  Allocated on
#   every entry (both the recursive case AND the base case need
#   to save $ra so they can return — the base case calls
#   print_solution, which clobbers $ra).
#
#         higher addresses
#           +-------------+
#           | col counter |   8($sp)   loop index, 0..N-1; must
#           |             |            survive across both
#           |             |            `jal safe` and `jal solve`
#           | saved row   |   4($sp)   reload-able input arg
#    $sp -> | saved $ra   |   0($sp)   each frame's own return
#           +-------------+            address
#         lower addresses
#
#   safe() and print_solution() use no stack frame.
#
#SYMBOL TABLE  (C variable -> MIPS location)
#
#   In main:
#     (nothing of its own — just `jal solve` with $a0=0)
#
#   In solve (per-call 12-byte frame; recursive):
#     row           $a0                  (input arg; reloaded from
#                                          4($sp) when needed)
#     col           8($sp)               (loop counter, 0..7;
#                                          stack-resident so each
#                                          recursive call has its own)
#     saved $ra     0($sp)               (each frame's return path)
#     saved row     4($sp)               (so we can compute row+1 and
#                                          set columns[row] after
#                                          syscalls/jals have
#                                          clobbered $a0)
#     columns       `columns` (.data)    (8 .word cells; mutated in
#                                          place as queens are placed)
#
#   In safe (no frame, no jal):
#     row           $a0                  (input arg)
#     col           $a1                  (input arg)
#     i             $t0                  (loop index 0..row-1)
#     &columns[i]   $t2                  (transient address)
#     c             $t3                  (transient: columns[i])
#     diff scratch  $t4, $t5             (transient: c - col, row - i,
#                                          col - c)
#     columns base  $t2 reuses           (no separate register)
#     return value  $v0                  (1 = safe, 0 = conflict)
#
#   In print_solution (no frame, no jal):
#     i             $t0                  (loop counter 0..N-1)
#     &columns[i]   $t3                  (transient address)
#     N (= 8)       $t1                  (constant for the bound check)
#
#   Cross-call saves (callee-save $s* values held LIVE across `jal`):
#     $s0   <- runtime's $ra        (saved in main before `jal solve`;
#                                    restored before the final `jr $ra`
#                                    out to the runtime)
#
#   Per-frame saves (stack-resident; the recursion pattern):
#     solve()'s $ra, row, and col loop counter all live on the
#     stack rather than in $s* registers, so each of the up-to-8
#     recursive frames has its own independent state.
#
#   Volatile:
#     $a0, $a1   syscall args / function args
#     $v0        syscall selector / function return value
#                (1 = print_int, 11 = print_char)

        .data
columns: .word 0, 0, 0, 0, 0, 0, 0, 0
N_const: .word 8                     # (not used; N is held as an
                                     #  immediate in `li $t1, 8` calls)

        .text
        .globl main
main:
        move $s0, $ra
        li $a0, 0                    # solve(0)
        jal solve
        move $ra, $s0
        jr $ra


# ---------- void solve(int row) -------------------------------------
# Per-call 12-byte frame: 0=$ra, 4=row, 8=col.
solve:
        addi $sp, $sp, -12
        sw $ra, 0($sp)
        sw $a0, 4($sp)               # save row

        # if (row == N) print_solution(); return;
        li $t0, 8
        bne $a0, $t0, solve_recurse

        jal print_solution
        lw $ra, 0($sp)
        addi $sp, $sp, 12
        jr $ra

solve_recurse:
        sw $0, 8($sp)                # col = 0

solve_col_loop:
        lw $t0, 8($sp)               # col
        li $t1, 8
        bge $t0, $t1, solve_done

        # safe(row, col)
        lw $a0, 4($sp)               # row
        move $a1, $t0                # col
        jal safe

        beqz $v0, solve_skip         # not safe -> try next col

        # columns[row] = col
        lw $t0, 4($sp)               # row
        sll $t1, $t0, 2
        la $t2, columns
        add $t2, $t2, $t1            # &columns[row]
        lw $t3, 8($sp)               # col
        sw $t3, ($t2)

        # solve(row + 1)
        lw $a0, 4($sp)
        addi $a0, $a0, 1
        jal solve

solve_skip:
        lw $t0, 8($sp)               # col
        addi $t0, $t0, 1
        sw $t0, 8($sp)               # col++
        j solve_col_loop

solve_done:
        lw $ra, 0($sp)
        addi $sp, $sp, 12
        jr $ra


# ---------- int safe(int row, int col) ------------------------------
# No frame; pure $t*+$a*+$v0.
# Input:  $a0 = row, $a1 = col
# Output: $v0 = 1 if safe, 0 if conflicts with an earlier queen
safe:
        li $t0, 0                    # i = 0

safe_loop:
        bge $t0, $a0, safe_yes       # i >= row -> all clear

        # c = columns[i]
        sll $t1, $t0, 2
        la $t2, columns
        add $t2, $t2, $t1            # &columns[i]
        lw $t3, ($t2)                # c

        # if (c == col) return 0
        beq $t3, $a1, safe_no

        # if (c - col == row - i) return 0    (one diagonal)
        sub $t4, $t3, $a1            # c - col
        sub $t5, $a0, $t0            # row - i
        beq $t4, $t5, safe_no

        # if (col - c == row - i) return 0    (other diagonal)
        sub $t4, $a1, $t3            # col - c
        beq $t4, $t5, safe_no

        addi $t0, $t0, 1             # i++
        j safe_loop

safe_yes:
        li $v0, 1
        jr $ra

safe_no:
        li $v0, 0
        jr $ra


# ---------- void print_solution(void) -------------------------------
# Walks columns[0..7], prints each as a decimal int followed by ' ',
# then a trailing '\n'.  No frame; no `jal` inside.
print_solution:
        li $t0, 0                    # i = 0

ps_loop:
        li $t1, 8
        bge $t0, $t1, ps_done

        sll $t2, $t0, 2
        la $t3, columns
        add $t3, $t3, $t2            # &columns[i]
        lw $a0, ($t3)
        li $v0, 1                    # print_int
        syscall

        li $a0, ' '
        li $v0, 11                   # print_char
        syscall

        addi $t0, $t0, 1
        j ps_loop

ps_done:
        li $a0, '\n'
        li $v0, 11
        syscall
        jr $ra
