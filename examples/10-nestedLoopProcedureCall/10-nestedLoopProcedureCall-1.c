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

/* Purpose */
// Do a nested loop, each of which iterates between -10 and 10 (inclusive)
// call a procedure "pmax" to determine which is the bigger of the two,
// and print out the results

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  void pmax(int32_t first, int32_t second); /*declaration*/

  for (int32_t i = -10; i <= 10; i++) {
    for (int32_t j = -10; j <= 10; j++) {
      pmax(i, j);
    }
  }
  exit(EXIT_SUCCESS);
}

void pmax(int32_t a1, int32_t a2) { /*definition*/
  int32_t biggest;

  if (a1 > a2) {
    biggest = a1;
  } else {
    biggest = a2;
  }

  printf("largest of %d and %d is %d\n", a1, a2, biggest);
}
