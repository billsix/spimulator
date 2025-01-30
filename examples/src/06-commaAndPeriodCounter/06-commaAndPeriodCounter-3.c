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
// Read from std in until EOF (Ctrl-D on Linux)
// Print out number of command and periods

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "operatingsystemfunctions.h"

/*
struct main_stack_frame {
  int32_t comma_count;
  int32_t period_count;
  char this_char;
  int32_t return_code;
};
*/

#define MAIN_STACK_FRAME_OFFSET_TO_COMMA_COUNT 0
#define MAIN_STACK_FRAME_OFFSET_TO_STOP_COUNT \
  (MAIN_STACK_FRAME_OFFSET_TO_COMMA_COUNT + SIZE_OF_INT32_T)
#define MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR \
  (MAIN_STACK_FRAME_OFFSET_TO_STOP_COUNT + SIZE_OF_INT32_T)
#define MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE \
  (MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR + SIZE_OF_INT32_T)
#define SIZE_OF_MAIN_STACK_FRAME \
  (MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE + SIZE_OF_INT32_T)

int main(int argc, char *argv[]) {
  // the frame pointer is the current stack frame, aka, where the local
  // variables are
  frame_pointer = frame_pointer - SIZE_OF_MAIN_STACK_FRAME;

  /*
  struct main_stack_frame main_stack_frame =
    {
      .comma_count = 0,
      .period_count = 0,
      .this_char = 0,
      .return_code = EXIT_SUCCESS
    };
  */

  // set comma count
  {
    int32_t comma_count_in_instruction = 0;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_COMMA_COUNT,
            /*src*/ &comma_count_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  // set stop count
  {
    int32_t period_count_in_instruction = 0;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_STOP_COUNT,
            /*src*/ &period_count_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  // set this_char
  {
    int32_t this_char_in_instruction = 0;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR,
            /*src*/ &this_char_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
  // set return_code
  {
    int32_t return_code_in_instruction = EXIT_SUCCESS;
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
            /*src*/ &return_code_in_instruction,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }

  {
    char readCharacter = operating_system_read_char();
    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR,
            /*src*/ &readCharacter,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
loopBegin: {
  char this_char_in_register;
  xmemcpy(/*dest*/ &this_char_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR,
          /*numberOfBytes*/ SIZE_OF_INT32_T);
  if (this_char_in_register == EOF) goto loopEnd;
}
  {
    char this_char_in_register;
    xmemcpy(/*dest*/ &this_char_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
    if (this_char_in_register != '.') goto notAPeriod;
  }
  {
    char period_count_in_register;
    xmemcpy(/*dest*/ &period_count_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_STOP_COUNT,
            /*numberOfBytes*/ SIZE_OF_INT32_T);

    period_count_in_register = period_count_in_register + 1;

    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_STOP_COUNT,
            /*src*/ &period_count_in_register,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
notAPeriod: {
  char this_char_in_register;
  xmemcpy(/*dest*/ &this_char_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR,
          /*numberOfBytes*/ SIZE_OF_INT32_T);

  if (this_char_in_register != ',') goto notAComma;
}
  {
    char comma_count_in_register;
    xmemcpy(/*dest*/ &comma_count_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_COMMA_COUNT,
            /*numberOfBytes*/ SIZE_OF_INT32_T);

    comma_count_in_register = comma_count_in_register + 1;

    xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_COMMA_COUNT,
            /*src*/ &comma_count_in_register,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
  }
notAComma: {
  char readCharacter = operating_system_read_char();
  xmemcpy(/*dest*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_THIS_CHAR,
          /*src*/ &readCharacter,
          /*numberOfBytes*/ SIZE_OF_INT32_T);
  goto loopBegin;
}

loopEnd: {
  char comma_count_in_register;
  xmemcpy(/*dest*/ &comma_count_in_register,
          /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_COMMA_COUNT,
          /*numberOfBytes*/ SIZE_OF_INT32_T);
  operating_system_print_int(comma_count_in_register);
}
  operating_system_print_string(" commas, ");
  {
    char period_count_in_register;
    xmemcpy(/*dest*/ &period_count_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_STOP_COUNT,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
    operating_system_print_int(period_count_in_register);
  }
  operating_system_print_string(" stops\n");
  {
    char return_code_in_register;
    xmemcpy(/*dest*/ &return_code_in_register,
            /*src*/ frame_pointer + MAIN_STACK_FRAME_OFFSET_TO_RETURN_VALUE,
            /*numberOfBytes*/ SIZE_OF_INT32_T);
    frame_pointer = frame_pointer + SIZE_OF_MAIN_STACK_FRAME;
    return return_code_in_register;
  }
}
