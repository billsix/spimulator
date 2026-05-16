// Copyright (c) 2021-2026 William Emerison Six
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/* PURPOSE: Solve the classic 8-Queens problem and print every
 *          solution.  Each solution is a single line of 8
 *          space-separated column indices — `columns[row]` is
 *          the column the queen on `row` sits in.
 *
 *          The first line printed is:
 *
 *              0 4 7 5 2 6 1 3
 *
 *          which means "row 0 has its queen in column 0, row 1
 *          in column 4, …".  The program emits exactly 92
 *          distinct solutions (the famous count for 8-queens).
 *
 *          Ninth and final demo from PLAN-cs-demos.md.  The
 *          shape it introduces is **backtracking** — the
 *          algorithmic pattern of "try a candidate, check, on
 *          success recurse deeper, on failure unwind and try the
 *          next candidate."
 *
 *          Three functions:
 *
 *             - `safe(row, col)` — does placing a queen at
 *               (row, col) conflict with any queen already
 *               placed at rows 0..row-1?  Checks same-column
 *               and same-diagonal in one pass.  No recursion.
 *
 *             - `solve(row)` — for each column c in 0..7, if
 *               safe(row, c), tentatively place the queen
 *               there and recurse to solve(row+1).  When row
 *               reaches 8, the board is complete and we print.
 *               When the column loop ends without recursing,
 *               we just return — that's the "unwind" half of
 *               backtracking (nothing to actively undo because
 *               the next iteration overwrites columns[row]).
 *
 *             - `print_solution()` — walk columns[0..7] and
 *               emit them space-separated, one trailing '\n'.
 *
 *          On the asm side this is the deepest stack-frame
 *          demo: solve()'s per-call frame holds saved $ra,
 *          saved row, AND the col loop counter (which has to
 *          survive across both `jal safe` and `jal solve`).
 *          Up to 8 frames stacked at peak recursion (rows 0..7),
 *          well within spim's 64 KiB default stack.
 *
 *          Invocation:
 *              spimulator -f 33-queens.asm
 *              ./33-queens-1                            # on Linux
 */

#include "io.h"

#define N 8

static int columns[N];

static int safe(int row, int col) {
  for (int i = 0; i < row; i++) {
    int c = columns[i];
    if (c == col) return 0;             // same column
    if (c - col == row - i) return 0;   // one diagonal
    if (col - c == row - i) return 0;   // the other diagonal
  }
  return 1;
}

static void print_solution(void) {
  for (int i = 0; i < N; i++) {
    print_int(columns[i]);
    print_char(' ');
  }
  print_char('\n');
}

static void solve(int row) {
  if (row == N) {
    print_solution();
    return;
  }
  for (int col = 0; col < N; col++) {
    if (safe(row, col)) {
      columns[row] = col;
      solve(row + 1);
    }
  }
}

__attribute__((noreturn)) void _start(void) {
  solve(0);
  os_exit(0);
}
