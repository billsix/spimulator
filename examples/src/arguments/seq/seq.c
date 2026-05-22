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

/* PURPOSE: A simplified port of `sbase/seq` — print integers
 *          M..N inclusive, one per line.
 *
 *          Invocation:
 *              seq N         -> 1..N
 *              seq M N       -> M..N
 *
 *          First demo with NO input I/O at all — the program is
 *          argv → algorithm → stdout.  Useful as a pipeline
 *          source: `seq 100 | head -n 5`.
 *
 *          Real `seq` also takes a step (`seq M S N`) and a
 *          format string; both out of scope.
 */

#include "io.h"
#include "crt0.h"

int my_main(int argc, char **argv) {
  int m, n;
  if (argc == 2) {
    m = 1;
    n = parse_int(argv[1]);
  } else if (argc == 3) {
    m = parse_int(argv[1]);
    n = parse_int(argv[2]);
  } else {
    print_string("usage: seq N | seq M N\n");
    return 1;
  }
  for (int i = m; i <= n; i++) {
    print_int(i);
    print_char('\n');
  }
  return 0;
}
