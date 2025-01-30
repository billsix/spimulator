// Copyright (c) 2021-2024 William Emerison Six
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
//

// Purpose

/* Define a couple of ints.  Increment them.  Compute addition of various
 * integers. */
/* Print out the results */

#include <inttypes.h>
#include <stdlib.h>

#include "operatingsystemfunctions.h"

int main(int argc, char *argv[]) {
  int32_t a, b;
  a = b = 5;
  operating_system_print_int(++a + 5);
  operating_system_print_string("\n");
  operating_system_print_int(a);
  operating_system_print_string("\n");
  operating_system_print_int(b++ + 5);
  operating_system_print_string("\n");
  operating_system_print_int(b);
  operating_system_print_string("\n");
  exit(EXIT_SUCCESS);
}
