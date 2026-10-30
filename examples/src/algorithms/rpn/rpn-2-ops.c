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

/* PURPOSE: Rung 2 of the RPN ladder (tasks/rpn-incremental-versions.md).  Adds
 * the four operators '+', '-', '*', '/' to rung 1's stack of integers: an
 * operator POPS the top two values, applies the operation, and PUSHES the
 * result.  Still integers ('/' truncates, so 1 3 / is 0), no error handling.
 *
 *     $ printf '3 4 + 2 * 1 3 / -\n' | spimulator -f rpn-2-ops.asm
 *     14
 *
 * The one new idea: the stack-machine STEP — pop two, apply, push one.  RPN has
 * no precedence, so all four operators are the same shape; they act immediately,
 * left-to-right, as they are read.  (Contrast calc, where '*'/'/' needed a
 * precedence layer.)  The paired rpn-2-ops.asm pops/pushes words on the $sp
 * stack.
 */

#include "crt0.h" /* provides _start; calls my_main(argc, argv) */
#include "io.h"

#define STACK_MAX 64 /* maximum evaluation-stack depth */

/* True for the four binary operators this calculator understands. */
static int is_operator(int c) {
  return c == '+' || c == '-' || c == '*' || c == '/';
}

int my_main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  int stack[STACK_MAX];
  int depth = 0;
  int c = read_char();

  while (c != -1) {
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
      c = read_char();
      continue;
    }
    if (c >= '0' && c <= '9') {
      int value = 0;
      while (c >= '0' && c <= '9') {
        value = value * 10 + (c - '0');
        c = read_char();
      }
      stack[depth++] = value;
      continue; /* c already holds the next unconsumed character */
    }
    if (is_operator(c)) {
      int right = stack[--depth];
      int left = stack[--depth];
      int result;
      if (c == '+') {
        result = left + right;
      } else if (c == '-') {
        result = left - right;
      } else if (c == '*') {
        result = left * right;
      } else {
        result = left / right; /* integer division, truncating */
      }
      stack[depth++] = result;
      c = read_char();
      continue;
    }
    c = read_char(); /* well-formed input assumed */
  }

  print_int(stack[depth - 1]); /* top of stack */
  print_char('\n');
  return 0;
}
