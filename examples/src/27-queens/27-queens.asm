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


# C source — see 27-queens.c
#
#     int N_global;  static int columns[12];
#     int safe(int row, int col) { ...checks columns[0..row-1]... }
#     void solve(int row) {
#       if (row == N_global) { print_solution(); return; }
#       for (int col = 0; col < N_global; col++)
#         if (safe(row, col)) { columns[row] = col; solve(row+1); }
#     }
#     int my_main(int argc, char **argv) {
#       N_global = (argc == 2) ? parse_int(argv[1]) : 8;
#       solve(0); return 0;
#     }


#PURPOSE:  N-Queens with N from argv.  Default 8 (92 solutions).
#          Capped at 12 (14200 solutions).


#STORAGE LAYOUT
#
#   solve()'s per-call 12-byte frame is unchanged from the 8-queens
#   version — the only difference is that the col-loop bound is
#   loaded from the global N_global instead of being a literal 8.
#
#         higher addresses
#           +-------------+
#           | col counter |   8($sp)
#           | saved row   |   4($sp)
#    $sp -> | saved $ra   |   0($sp)
#           +-------------+
#         lower addresses
#
#SYMBOL TABLE (relevant subset)
#
#   N_global      `N_global` (.data)   (loaded at start of solve)
#   columns       `columns` (.data)    (12 .word cells; MAX_N=12)

        .data
columns:    .word 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
N_global:   .word 8
usageMsg:   .asciiz "usage: queens [N]   (1 <= N <= 12)\n"

        .text
        .globl main
main:
        move $s0, $ra

        # argc dispatch
        li $t0, 1
        beq $a0, $t0, run            # argc == 1 -> default 8
        li $t0, 2
        bne $a0, $t0, usage

        lw $a0, 4($a1)
        jal atoi
        # bounds check 1..12
        blez $v0, usage
        li $t0, 12
        bgt $v0, $t0, usage
        # store N_global
        la $t0, N_global
        sw $v0, ($t0)

run:
        li $a0, 0                    # solve(0)
        jal solve

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


# ---------- void solve(int row) -------------------------------------
solve:
        addi $sp, $sp, -12
        sw $ra, 0($sp)
        sw $a0, 4($sp)

        # if (row == N_global) print_solution(); return;
        la $t0, N_global
        lw $t0, ($t0)
        bne $a0, $t0, solve_recurse

        jal print_solution
        lw $ra, 0($sp)
        addi $sp, $sp, 12
        jr $ra

solve_recurse:
        sw $0, 8($sp)                # col = 0

solve_col_loop:
        lw $t0, 8($sp)               # col
        la $t1, N_global
        lw $t1, ($t1)
        bge $t0, $t1, solve_done

        # safe(row, col)
        lw $a0, 4($sp)
        move $a1, $t0
        jal safe

        beqz $v0, solve_skip

        # columns[row] = col
        lw $t0, 4($sp)
        sll $t1, $t0, 2
        la $t2, columns
        add $t2, $t2, $t1
        lw $t3, 8($sp)
        sw $t3, ($t2)

        # solve(row + 1)
        lw $a0, 4($sp)
        addi $a0, $a0, 1
        jal solve

solve_skip:
        lw $t0, 8($sp)
        addi $t0, $t0, 1
        sw $t0, 8($sp)
        j solve_col_loop

solve_done:
        lw $ra, 0($sp)
        addi $sp, $sp, 12
        jr $ra


# ---------- int safe(int row, int col) ------------------------------
safe:
        li $t0, 0                    # i = 0

safe_loop:
        bge $t0, $a0, safe_yes

        sll $t1, $t0, 2
        la $t2, columns
        add $t2, $t2, $t1
        lw $t3, ($t2)                # c

        beq $t3, $a1, safe_no

        sub $t4, $t3, $a1
        sub $t5, $a0, $t0
        beq $t4, $t5, safe_no

        sub $t4, $a1, $t3
        beq $t4, $t5, safe_no

        addi $t0, $t0, 1
        j safe_loop

safe_yes:
        li $v0, 1
        jr $ra

safe_no:
        li $v0, 0
        jr $ra


# ---------- void print_solution(void) -------------------------------
print_solution:
        li $t0, 0                    # i = 0

ps_loop:
        la $t1, N_global
        lw $t1, ($t1)
        bge $t0, $t1, ps_done

        sll $t2, $t0, 2
        la $t3, columns
        add $t3, $t3, $t2
        lw $a0, ($t3)
        li $v0, 1
        syscall

        li $a0, ' '
        li $v0, 11
        syscall

        addi $t0, $t0, 1
        j ps_loop

ps_done:
        li $a0, '\n'
        li $v0, 11
        syscall
        jr $ra


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
