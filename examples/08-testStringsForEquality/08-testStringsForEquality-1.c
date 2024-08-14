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
// Test strings for equality and print out the results on stdout

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "operatingsystemfunctions.h"

/*
* Compare
  two strings for equality.
* Return 'false' if they are.
*/
bool str_eq(const char *s1, const char *s2) {
  while (*s1 == *s2) {
    /*
     * At end of string return 0.
     */
    if (*s1 == 0) return false;
    s1++;
    s2++;
  }
  /* Difference detected! */
  return true;
}

int main(int argc, char *argv[]) {
  char *str1 = "str1";
  char *str2 = "str2";
  char *str3 = "str1";

  operating_system_print_string("str1 compared to str2 is ");
  operating_system_print_int(str_eq(str1, str2));
  operating_system_print_string("\n");
  operating_system_print_string("str1 compared to str3 is ");
  operating_system_print_int(str_eq(str1, str3));
  operating_system_print_string("\n");
  operating_system_print_string("str2 compared to str3 is ");
  operating_system_print_int(str_eq(str2, str3));
  operating_system_print_string("\n");
  return 0;
}
