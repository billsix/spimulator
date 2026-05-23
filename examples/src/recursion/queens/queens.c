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

/* PURPOSE: Solve the N-Queens problem and print every solution.
 *          Each solution is a single line of N space-separated
 *          column indices — `columns[row]` is the column the
 *          queen on `row` sits in.
 *
 *          Invocation:
 *              queens            -> N=8 (default; 92 solutions)
 *              queens N          -> N from argv (1 <= N <= 12)
 *
 *          The famous count for 8-queens is 92.  For larger N
 *          the count grows fast: 9 → 352, 10 → 724, 11 → 2680,
 *          12 → 14200.  Capped at 12 so the output is
 *          finite-ish.
 *
 *          The asm-side lesson is backtracking with a per-call
 *          12-byte stack frame.  The `col` loop counter must
 *          live on the stack because each recursive `solve`
 *          call has its own.
 */

#include "io.h"
#include "crt0.h"

#define MAX_N 12
#define DEFAULT_N 8

static int columns[MAX_N];
static int N_global;

static int safe(int row, int col) {
  for (int i = 0; i < row; i++) {
    int c = columns[i];
    if (c == col) return 0;            // same column
    if (c - col == row - i) return 0;  // one diagonal
    if (col - c == row - i) return 0;  // the other diagonal
  }
  return 1;
}

static void print_solution(void) {
  for (int i = 0; i < N_global; i++) {
    print_int(columns[i]);
    print_char(' ');
  }
  print_char('\n');
}

static void solve(int row) {
  if (row == N_global) {
    print_solution();
    return;
  }
  for (int col = 0; col < N_global; col++) {
    if (safe(row, col)) {
      columns[row] = col;
      solve(row + 1);
    }
  }
}

int my_main(int argc, char** argv) {
  N_global = DEFAULT_N;
  if (argc == 2) {
    N_global = parse_int(argv[1]);
    if (N_global < 1 || N_global > MAX_N) {
      print_string("usage: queens [N]   (1 <= N <= 12)\n");
      return 1;
    }
  } else if (argc > 2) {
    print_string("usage: queens [N]\n");
    return 1;
  }
  solve(0);
  return 0;
}
