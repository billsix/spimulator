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

/* PURPOSE: ROT13 — read stdin and write stdout, with each
 *          alphabetic byte rotated 13 places forward in the
 *          alphabet (wrapping around past 'z' back to 'a',
 *          and past 'Z' back to 'A').  Non-alphabetic bytes
 *          pass through unchanged.
 *
 *          Fifth demo from PLAN-cs-demos.md.  Shape matches
 *          13-tr exactly — byte stream in, byte stream out,
 *          conditional per-byte transformation — but the
 *          transformation itself is more interesting:
 *
 *             ch -> 'a' + (ch - 'a' + 13) % 26
 *
 *          The modulo 26 is the new asm idea: turning "shift
 *          by 13 with wraparound" into a `div`+`mfhi`.  Without
 *          the modulo a naive `addi $t, $t, 13` would push 'p'
 *          past 'z' and produce garbage.
 *
 *          ROT13 is self-inverse: running the program twice on
 *          the same input gets the input back.  That's a useful
 *          way to verify the implementation:
 *
 *              echo "Hello, World!" | ./14-rot13-1
 *              # => Uryyb, Jbeyq!
 *              echo "Uryyb, Jbeyq!" | ./14-rot13-1
 *              # => Hello, World!
 *
 *          (Also why ROT13 is sometimes called "decoder ring
 *          encryption" — encoding and decoding are the same
 *          operation.)
 *
 *          Both the C and asm versions terminate on real EOF —
 *          `read_char` returns -1 in both directions
 *          (`os_read returning 0` on the C side; spim's
 *          syscall 12 returning -1 on the asm side).  No
 *          sentinel character is needed.
 */

#include "io.h"

__attribute__((noreturn)) void _start(void) {
  int ch;
  while ((ch = read_char()) != -1) {
    if (ch >= 'a' && ch <= 'z')
      ch = 'a' + (ch - 'a' + 13) % 26;
    else if (ch >= 'A' && ch <= 'Z')
      ch = 'A' + (ch - 'A' + 13) % 26;
    print_char((char)ch);
  }
  os_exit(0);
}
