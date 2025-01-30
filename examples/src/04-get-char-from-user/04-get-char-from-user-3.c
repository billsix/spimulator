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

/* Get a character from the user, print out the value. */
/* Terminate when the character 'a' is read in */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "operatingsystemfunctions.h"

/*
struct main_stack_frame{
  char ch;
  int32_t return_code;
};
*/

#define MAIN_STACK_FRAME_OFFSET_TO_CH 0
#define MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE \
  (MAIN_STACK_FRAME_OFFSET_TO_CH +              \
   SIZE_OF_INT32_T)  // even though CH is a character, the subsequent
                     // int needs to be aligned on a 32-bit boundary
#define SIZE_OF_MAIN_STACK_FRAME \
  (MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE + SIZE_OF_INT32_T)

int main(int argc, char *argv[]) {
  // the frame pointer is the current stack frame, aka, where the local
  // variables are
  frame_pointer = frame_pointer - SIZE_OF_MAIN_STACK_FRAME;

  //   struct main_stack_frame main_stack_frame = {.ch = 0, .return_code = 0};
  {
    int32_t ch_in_instruction = 0;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
            /*src*/ &ch_in_instruction,
            /*numberOfBytes*/ SIZE_OF_BYTE);
    int32_t return_code_in_instruction = 0;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
            /*src*/ &return_code_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }

  {
    // main_stack_frame.ch = operating_system_read_char();
    char ch_in_register = operating_system_read_char();
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
            /*src*/ &ch_in_register,
            /*numberOfBytes*/ SIZE_OF_BYTE);
  }
loopTest: {
  char ch_in_register;
  // if (! (main_stack_frame.ch != 'a')) goto loopEnd;
  xmemcpy(/*dest*/ &ch_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
          /*numberOfBytes*/ SIZE_OF_BYTE);
  if (!(ch_in_register != 'a')) goto loopEnd;
}
loopBody: {
  // if (! (main_stack_frame.ch != '\n')) goto getNextChar;
  char ch_in_register;
  xmemcpy(/*dest*/ &ch_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
          /*numberOfBytes*/ SIZE_OF_BYTE);
  if (!(ch_in_register != '\n')) goto getNextChar;
}
  operating_system_print_string("ch was ");
  {
    char ch_in_register;
    xmemcpy(/*dest*/ &ch_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
            /*numberOfBytes*/ SIZE_OF_BYTE);
    operating_system_print_char(ch_in_register);
  }
  operating_system_print_string(", value ");
  {
    char ch_in_register;
    xmemcpy(/*dest*/ &ch_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
            /*numberOfBytes*/ SIZE_OF_BYTE);
    operating_system_print_int(ch_in_register);
  }
  operating_system_print_string("\n");
getNextChar: {
  char ch_in_register = operating_system_read_char();
  xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_CH,
          /*src*/ &ch_in_register,
          /*numberOfBytes*/ SIZE_OF_BYTE);
  goto loopTest;
}
loopEnd: {
  int32_t return_code_in_register;
  xmemcpy(/*dest*/ &return_code_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
          /*numberOfBytes*/ SIZE_OF_INT32_T);
  frame_pointer = frame_pointer + SIZE_OF_MAIN_STACK_FRAME;
  return return_code_in_register;
}
}
