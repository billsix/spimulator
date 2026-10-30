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

/* PURPOSE: Rung 1 of the RPN ladder (tasks/rpn-incremental-versions.md).  The
 * smallest step toward the rpn stack machine: read whitespace-separated whole
 * INTEGERS, PUSH each onto an evaluation stack, and at end of input print the
 * TOP of the stack.  No operators yet; integers (the FPU arrives at rung 3);
 * well-formed input assumed (error handling is the final rung, rpn).
 *
 *     $ printf '3 4 5\n' | spimulator -f rpn-1-stack.asm
 *     5
 *
 * The one new idea: the EVALUATION STACK (here, just pushes).  The C uses an
 * array + a depth index; the paired rpn-1-stack.asm uses the real MIPS $sp
 * stack (push = addi $sp,-4 + sw), exactly as the finished rpn uses it for
 * doubles.  Reading is byte-at-a-time via read_char() (syscall 12, -1 at EOF).
 */

#include "crt0.h" /* provides _start; calls my_main(argc, argv) */
#include "io.h"

#define STACK_MAX 64 /* maximum evaluation-stack depth */

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
    c = read_char(); /* well-formed input assumed: nothing else appears yet */
  }

  print_int(stack[depth - 1]); /* top of stack */
  print_char('\n');
  return 0;
}
