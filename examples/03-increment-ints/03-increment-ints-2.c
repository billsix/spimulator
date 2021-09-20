// Permission is hereby granted for anyone to do anything that they
// want with this material—you may freely reprint it, redistribute it,
// amend it or do whatever you like with it provided that
// you include an acknowledgement of the original authorship and copyright in
// the form of a link to this page. In doing so
// you must accept that you do so strictly on your own
// liability and that you accept any consequences with no liability whatsoever
// remaining with the original authors. If you find the material useful
// and happen to encounter one of the authors, it is unlikely
// that they will refuse offers to buy them a drink. You
// may therefore like to consider this material ‘drinkware’. (Offer void where
// prohibited by law, in which case fawning and flattery may be substituted.)

// https://publications.gbdirect.co.uk//c_book/copyright.html

// Copyright (c) 2021 William Emerison Six
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

#include "platformabstraction.h"

struct main_stack_frame {
  int32_t a;
  int32_t b;
  int32_t return_value;
};

int main(int argc, char *argv[]) {

  struct main_stack_frame msf = {.a = 5, .b = 5, .return_value = EXIT_SUCCESS};
  struct main_stack_frame *main_stack_frame = &msf;

  // the first version of the C code uses preincement
  main_stack_frame->a++;
  print_int(main_stack_frame->a + 5);
  print_string("\n");

  print_int(main_stack_frame->a);
  print_string("\n");

  print_int(main_stack_frame->b + 5);
  print_string("\n");
  // the first version of the C code uses postincrement
  main_stack_frame->b++;

  print_int(main_stack_frame->b);
  print_string("\n");

  return main_stack_frame->return_value;
}
