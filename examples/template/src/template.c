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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

// define CPU
#define x86_64_linux
// #define spim

// define sizes based on the CPU
#ifdef x86_64_linux
#define SIZE_OF_INT32_T 4
#define SIZE_OF_BYTE_ADDRESS 8
#endif

#ifdef spim
#define SIZE_OF_INT32_T 4
#define SIZE_OF_ADDRESS_OF_BYTE 4
#endif

#define BYTE uint8_t
#define ADDRESS_OF_BYTE BYTE *

#define KILOBYTE 1024
#define MEGABYTE (KILOBYTE * KILOBYTE)

// define the Random Access Memory that will be available to our program
#define RAM_SIZE (10 * MEGABYTE)
BYTE random_access_memory[RAM_SIZE];

// byte of the begining of the frame
ADDRESS_OF_BYTE frame_pointer =
    (ADDRESS_OF_BYTE)(random_access_memory + RAM_SIZE);

int main(int argc, char *argv[]) {
  // step 1
  //   replace all IO formatting operations with iolib operations, because
  //   they are easily ported to MIPS
  // step 2
  //   replace all loops with if statements and gotos
  // step 3
  //   all gotos must explicitly if(pred) goto line10; else goto line 20;
  // step 4
  //   for each procedure, make all named local variables into a struct.
  //   for instance, for procedure foo, call it "foo_stack_frame"
  //   make all variable access relative to that struct
  // step 5.
  //   inline all procedures except for main and for IO.
  //   this requires returning a procedures return value
  //   to the caller's stack frame, restoring the caller's stack frame,
  //   and goto the memory address of the caller's instruction
  //   that occurs after this procedure call
  // step 6.
  //   for each stack frame, at compile time, calculate the offset
  //   of each variable in the stack frame.  use memcpy
  //   to put each variable on or off of the stack

  exit(EXIT_SUCCESS);
}
