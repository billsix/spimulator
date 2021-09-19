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
// Test strings for equality and print out the results on stdout

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXSTRING 50 /* max no. of strings */
#define MAXLEN 80    /* max length. of strings */

void print_arr(const char *p_array[]);
void sort_arr(const char *p_array[]);
char *next_string(char *destination);

int main(int argc, char *argv[]) {
  /* leave room for null at end */
  const char *p_array[MAXSTRING + 1];

  /* storage for strings */
  char strings[MAXSTRING][MAXLEN];

  /* count of strings read */
  size_t nstrings = 0;
  while (nstrings < MAXSTRING && next_string(strings[nstrings]) != 0) {

    p_array[nstrings] = strings[nstrings];
    nstrings++;
  }
  /* terminate p_array */
  p_array[nstrings] = 0;

  sort_arr(p_array);
  print_arr(p_array);
  exit(EXIT_SUCCESS);
}

void print_arr(const char *p_array[]) {
  for (size_t index = 0; p_array[index] != 0; index++)
    printf("%s\n", p_array[index]);
}

void sort_arr(const char *p_array[]) {
  for (size_t low_index = 0;
       p_array[low_index] != 0 && p_array[low_index + 1] != 0; low_index++) {

    for (size_t hi_index = low_index + 1; p_array[hi_index] != 0; hi_index++) {

      int32_t comp_val = strcmp(p_array[hi_index], p_array[low_index]);
      if (comp_val >= 0)
        continue;
      /* swap strings */
      const char *tmp = p_array[hi_index];
      p_array[hi_index] = p_array[low_index];
      p_array[low_index] = tmp;
    }
  }
}

char *next_string(char *destination) {
  char *cp = destination;
  char c;
  while ((c = getchar()) != '\n' && c != EOF) {
    if (cp - destination < MAXLEN - 1)
      *cp++ = c;
  }
  *cp = 0;
  if (c == EOF && cp == destination)
    return 0;
  return destination;
}
