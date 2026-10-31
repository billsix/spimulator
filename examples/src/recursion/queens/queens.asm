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


# C source — see queens.c


# Invocations:
#   spimulator -f queens.asm           # 8-queens (92 solutions, default)
#   spimulator -f queens.asm 6         # 6-queens (4 solutions)
#   spimulator -f queens.asm 12        # 12-queens (14200; max)


#PURPOSE:  N-Queens with N from argv.  Default 8 (92 solutions).
#          Capped at 12 (14200 solutions).  The lesson is backtracking
#          with a per-call stack frame in solve().
#
#          STACK-VISIBLE STYLE: solve() recurses, so its C locals —
#          the parameter `row` and the loop counter `col` — live in
#          THIS call's own frame at fixed -N($fp) offsets, next to the
#          saved $ra and $fp.  Each recursive call gets an independent
#          frame, so `col` is never clobbered by a deeper call.  The
#          leaf helpers safe() and print_solution() make no `jal`, so
#          they save $fp (not $ra) and home their loop index `i`.  The
#          board state `columns[]` and the bound `N_global` are C
#          GLOBALS, so they stay in .data, not on any frame.
#          Style reference: tasks/reference/mips-asm-teaching-style.md
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr      | register   | C type    | bytes | role / notes
#  ------------+------------------+------------+-----------+-------+---------------------------
#  argc        | —                | $a0        | int       |   4   | entry only; 1 -> default 8, 2 -> parse N
#  argv        | —                | $a1        | char **   |   4   | entry only; argv[1] fetched for atoi
#  N           | N_global (.data) | $v0 (copy) | int       |   4   | parsed, bounds-checked, stored to the global
#  (saved $ra) | -4($fp)          | —          | code addr |   4   | jal atoi / jal solve
#  (saved $fp) | -8($fp)          | —          | frame ptr |   4   | caller's frame pointer
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: solve — recursive; one frame PER call)
#
#  C variable  | memory addr | register   | C type    | bytes | role / notes
#  ------------+-------------+------------+-----------+-------+---------------------------
#  (arg) row   | -12($fp)    | $a0 (copy) | int       |   4   | which row we are placing a queen on
#  col         | -16($fp)    | $t0 (copy) | int       |   4   | column loop 0..N-1 for this row
#  (saved $ra) | -4($fp)     | —          | code addr |   4   | jal safe / jal solve / jal print_solution
#  (saved $fp) | -8($fp)     | —          | frame ptr |   4   | caller solve (or main) frame's $fp
#  columns[]   | columns (.data) | —      | int[12]   |  48   | GLOBAL: columns[r] = chosen column for row r
#  N_global    | N_global (.data) | —     | int       |   4   | GLOBAL: board size / recursion depth
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: safe — leaf helper, no jal)
#
#  C variable  | memory addr | register   | C type | bytes | role / notes
#  ------------+-------------+------------+--------+-------+---------------------------
#  (arg) row   | —           | $a0        | int    |   4   | row being tested; read-only
#  (arg) col   | —           | $a1        | int    |   4   | candidate column; read-only
#  i           | -8($fp)     | $t0 (copy) | int    |   4   | scans earlier rows 0..row-1
#  c           | —           | $t3        | int    |   4   | transient columns[i]
#  (saved $fp) | -4($fp)     | —          | frame ptr |  4  | caller's frame pointer
#
# ------------------------------------------------------------------
#  SYMBOL TABLE (routine: print_solution — leaf helper, syscalls only)
#
#  C variable  | memory addr | register   | C type | bytes | role / notes
#  ------------+-------------+------------+--------+-------+---------------------------
#  i           | -8($fp)     | $t0 (copy) | int    |   4   | prints columns[0..N-1]
#  (saved $fp) | -4($fp)     | —          | frame ptr |  4  | caller's frame pointer
#
# ------------------------------------------------------------------
#  SNAPSHOT (input: queens 4; solve has placed row 0 (col 1) and row 1
#            (col 3) and is in solve(2) scanning columns.  Three frames
#            are live; $fp = innermost.  columns[] is the shared global.)
#
#      stack, higher addresses at top       .data (global board state)
#     +---------------------------+        +-----------------------------+
#     | solve(0): row=0 col=1 ... |        | columns: [1, 3, ?, ?]       |
#     +---------------------------+        | N_global: 4                 |
#     | solve(1): row=1 col=3 ... |        +-----------------------------+
#     +---------------------------+  solve(2):  <-- $fp here
#     | -4  saved $ra (-> solve 1)|
#     | -8  saved $fp (-> solve 1)|
#     | -12 row = 2               |
#     | -16 col = 0,1,2,...       |   (safe(2,col) fails for a while;
#     +---------------------------+    this branch dead-ends and backtracks)
#           $sp = $fp - 16
#
#   The full N=4 search finds two solutions: "1 3 0 2" and "2 0 3 1".
#
#  NO #CANONICAL block: each solution line ends with a trailing space
#  (a space is printed after every column, including the last), which a
#  comment-based expected-output anchor cannot carry robustly.
#  Correctness is gated by before/after output equivalence instead —
#  `queens 4` -> "1 3 0 2" / "2 0 3 1"; `queens 6` -> 4 lines; default
#  (8) -> 92 lines.

        .data
columns:    .word 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
N_global:   .word 8
usageMsg:   .asciiz "usage: queens [N]   (1 <= N <= 12)\n"

        .text
        .globl main
main:
        # ---- prologue: build main's 8-byte stack frame -------------
        addiu $sp, $sp, -8
        sw $ra, 4($sp)               # save $ra          -> -4($fp)
        sw $fp, 0($sp)               # save caller's $fp -> -8($fp)
        addiu $fp, $sp, 8

        # argc dispatch   (argc arrives in $a0)
        li $t0, 1
        beq $a0, $t0, run            # argc == 1 -> default 8
        li $t0, 2
        bne $a0, $t0, usage

        # N = atoi(argv[1]);   (argv arrives in $a1)
        lw $a0, 4($a1)
        jal atoi
        # if (N < 1 || N > 12) usage;
        blez $v0, usage
        li $t0, 12
        bgt $v0, $t0, usage
        # N_global = N;   (store into the global)
        la $t0, N_global
        sw $v0, ($t0)

run:
        # solve(0);
        li $a0, 0
        jal solve

        # ---- epilogue ----------------------------------------------
        li $v0, 0                    # exit status: __start passes this through syscall 17
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 8
        jr $ra

usage:
        li $v0, 4
        la $a0, usageMsg
        syscall
        li $a0, 1
        li $v0, 17
        syscall


# ---------- void solve(int row) -------------------------------------
# Recursive backtracking.  Each call owns a 16-byte frame holding
# $ra, $fp, row, and the column loop counter col.
solve:
        addiu $sp, $sp, -16
        sw $ra, 12($sp)              # save $ra  -> -4($fp)
        sw $fp, 8($sp)               # save $fp  -> -8($fp)
        addiu $fp, $sp, 16
        sw $a0, -12($fp)             # row

        # if (row == N_global) { print_solution(); return; }
        la $t0, N_global
        lw $t0, ($t0)
        lw $t1, -12($fp)             # row
        bne $t1, $t0, solve_recurse
        jal print_solution
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 16
        jr $ra

solve_recurse:
        # col = 0;
        li $t0, 0
        sw $t0, -16($fp)             # col = 0

solve_col_loop:
        lw $t0, -16($fp)             # $t0 = col
        la $t1, N_global
        lw $t1, ($t1)
        bge $t0, $t1, solve_done     # while (col < N)

        # if (safe(row, col)) { ... }
        lw $a0, -12($fp)             # row
        lw $a1, -16($fp)             # col
        jal safe
        beqz $v0, solve_skip

        # columns[row] = col;
        lw $t0, -12($fp)             # row
        sll $t0, $t0, 2              # row * 4
        la $t1, columns
        add $t1, $t1, $t0            # &columns[row]
        lw $t2, -16($fp)             # col
        sw $t2, ($t1)                # columns[row] = col

        # solve(row + 1);
        lw $a0, -12($fp)             # row
        addi $a0, $a0, 1             # row + 1
        jal solve

solve_skip:
        # col++;
        lw $t0, -16($fp)             # col
        addi $t0, $t0, 1
        sw $t0, -16($fp)             # store the new col back
        j solve_col_loop

solve_done:
        lw $ra, -4($fp)
        lw $fp, -8($fp)
        addiu $sp, $sp, 16
        jr $ra


# ---------- int safe(int row, int col) ------------------------------
# Leaf helper (no jal): can a queen sit at (row, col) given the queens
# already placed on rows 0..row-1?  row/col stay in $a0/$a1 (read-only,
# never clobbered in a leaf); the scan index i lives on the stack.
safe:
        addiu $sp, $sp, -8
        sw $fp, 4($sp)               # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 8

        li $t0, 0
        sw $t0, -8($fp)              # i = 0

safe_loop:
        lw $t0, -8($fp)              # $t0 = i
        bge $t0, $a0, safe_yes       # while (i < row)

        # c = columns[i];
        sll $t1, $t0, 2              # i * 4
        la $t2, columns
        add $t2, $t2, $t1
        lw $t3, ($t2)                # c = columns[i]

        # same column?        if (c == col) return 0;
        beq $t3, $a1, safe_no
        # same '\' diagonal?  if (c - col == row - i) return 0;
        sub $t4, $t3, $a1            # c - col
        sub $t5, $a0, $t0            # row - i
        beq $t4, $t5, safe_no
        # same '/' diagonal?  if (col - c == row - i) return 0;
        sub $t4, $a1, $t3            # col - c
        beq $t4, $t5, safe_no

        # i++;
        lw $t0, -8($fp)              # i
        addi $t0, $t0, 1
        sw $t0, -8($fp)
        j safe_loop

safe_yes:
        li $v0, 1
        lw $fp, -4($fp)
        addiu $sp, $sp, 8
        jr $ra

safe_no:
        li $v0, 0
        lw $fp, -4($fp)
        addiu $sp, $sp, 8
        jr $ra


# ---------- void print_solution(void) -------------------------------
# Leaf helper (syscalls only, no jal): print columns[0..N-1] separated
# by spaces, then a newline.  The index i lives on the stack (reloaded
# each pass) so no register has to survive the print syscalls.
print_solution:
        addiu $sp, $sp, -8
        sw $fp, 4($sp)               # save caller's $fp -> -4($fp)
        addiu $fp, $sp, 8

        li $t0, 0
        sw $t0, -8($fp)              # i = 0

ps_loop:
        lw $t0, -8($fp)              # $t0 = i
        la $t1, N_global
        lw $t1, ($t1)
        bge $t0, $t1, ps_done        # while (i < N)

        # print_int(columns[i]);
        lw $t0, -8($fp)              # i
        sll $t0, $t0, 2              # i * 4
        la $t1, columns
        add $t1, $t1, $t0
        lw $a0, ($t1)                # columns[i]
        li $v0, 1
        syscall

        # print_char(' ');
        li $a0, ' '
        li $v0, 11
        syscall

        # i++;
        lw $t0, -8($fp)              # i
        addi $t0, $t0, 1
        sw $t0, -8($fp)
        j ps_loop

ps_done:
        li $a0, '\n'
        li $v0, 11
        syscall
        lw $fp, -4($fp)
        addiu $sp, $sp, 8
        jr $ra


# ---------- atoi subroutine -----------------------------------------
# Shared leaf helper (string -> signed int), kept register-based.
#
#  SYMBOL TABLE (routine: atoi — leaf helper, register-based)
#
#  C variable | memory addr | register | C type | bytes | role / notes
#  -----------+-------------+----------+--------+-------+------------------------------
#  (arg) str  | —           | $a0      | char * |   4   | address of the string, walked
#  value      | —           | $v0      | int    |   4   | accumulator; becomes the return
#  sign       | —           | $t1      | int    |   4   | +1 or -1
#  digit      | —           | $t0      | int    |   4   | one byte decoded to 0..9
#  ten        | —           | $t2      | int    |   4   | constant multiplier
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
