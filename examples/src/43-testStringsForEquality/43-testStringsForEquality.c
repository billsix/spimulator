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

/* PURPOSE: Test strings for equality and print the results on
 *          stdout.
 *
 * NOTE: str_eq returns 0 when the strings are equal and 1 when
 *       they differ — intentionally the inverse of what the name
 *       suggests, matching the book demo this came from.
 */

#include "io.h"

int str_eq(const char *s1, const char *s2) {
  while (*s1 == *s2) {
    /* end of both strings reached together => equal */
    if (*s1 == 0) return 0;
    s1++;
    s2++;
  }
  /* mismatch */
  return 1;
}

__attribute__((noreturn)) void _start(void) {
  const char *str1 = "str1";
  const char *str2 = "str2";
  const char *str3 = "str1";

  print_string("str1 compared to str2 is ");
  print_int(str_eq(str1, str2));
  print_string("\n");
  print_string("str1 compared to str3 is ");
  print_int(str_eq(str1, str3));
  print_string("\n");
  print_string("str2 compared to str3 is ");
  print_int(str_eq(str2, str3));
  print_string("\n");
  os_exit(0);
}
