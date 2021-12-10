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

/* Get a character from the user, print out the value. */
/* Terminate when the character 'a' is read in */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "platformabstraction.h"

/*
struct main_stack_frame{
  char ch;
  int32_t return_code;
};
*/

#define MAIN_STACK_FRAME_OFFSET_TO_CH 0
#define MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE                                \
  (MAIN_STACK_FRAME_OFFSET_TO_CH +                                             \
   SIZE_OF_INT32_T) // even though CH is a character, the subsequent
                    // int needs to be aligned on a 32-bit boundary
#define SIZE_OF_MAIN_STACK_FRAME                                               \
  (MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE + SIZE_OF_INT32_T)

int main(int argc, char *argv[]) {

  // the frame pointer is the current stack frame, aka, where the local
  // variables are
  frame_pointer = frame_pointer - SIZE_OF_MAIN_STACK_FRAME;

  //   struct main_stack_frame main_stack_frame = {.ch = 0, .return_code = 0};
  {
    int32_t ch_in_register = 0;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
            /*src*/ &ch_in_register,
            /*numberOfBytes*/ SIZE_OF_BYTE);
    int32_t return_code_in_register = 0;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
            /*src*/ &return_code_in_register,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }

  {
    // main_stack_frame.ch = read_char();
    char ch_in_register = read_char();
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
            /*src*/ &ch_in_register,
            /*numberOfBytes*/ SIZE_OF_BYTE);
  }
loopTest : {
  char ch_in_register;
  // if (! (main_stack_frame.ch != 'a')) goto loopEnd;
  xmemcpy(/*dest*/ &ch_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
          /*numberOfBytes*/ SIZE_OF_BYTE);
  if (!(ch_in_register != 'a'))
    goto loopEnd;
}
loopBody : {
  // if (! (main_stack_frame.ch != '\n')) goto getNextChar;
  char ch_in_register;
  xmemcpy(/*dest*/ &ch_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
          /*numberOfBytes*/ SIZE_OF_BYTE);
  if (!(ch_in_register != '\n'))
    goto getNextChar;
}
  print_string("ch was ");
  {
    char ch_in_register;
    xmemcpy(/*dest*/ &ch_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
            /*numberOfBytes*/ SIZE_OF_BYTE);
    print_char(ch_in_register);
  }
  print_string(", value ");
  {
    char ch_in_register;
    xmemcpy(/*dest*/ &ch_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
            /*numberOfBytes*/ SIZE_OF_BYTE);
    print_int(ch_in_register);
  }
  print_string("\n");
getNextChar : {
  char ch_in_register = read_char();
  xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
          /*src*/ &ch_in_register,
          /*numberOfBytes*/ SIZE_OF_BYTE);
  goto loopTest;
}
loopEnd : {
  int32_t return_code_in_register;
  xmemcpy(/*dest*/ &return_code_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
          /*numberOfBytes*/ SIZE_OF_INT32_T);
  frame_pointer = frame_pointer + SIZE_OF_MAIN_STACK_FRAME;
  return return_code_in_register;
}
}
