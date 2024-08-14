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

/* Purpose */
//
//

#include <inttypes.h>
#include <stdlib.h>

#include "operatingsystemfunctions.h"

int32_t mxPlusB(int32_t m, int32_t x, int32_t b) {
  int32_t result = m * x + b;
  return result;
}

int main(int32_t argc, char *argv[]) {
  int32_t result1 = mxPlusB(1, 2, 3);
  operating_system_print_int(result1);
  operating_system_print_string("\n");
  int32_t result2 = mxPlusB(4, 5, 6);
  operating_system_print_int(result2);
  operating_system_print_string("\n");
  exit(EXIT_SUCCESS);
}
