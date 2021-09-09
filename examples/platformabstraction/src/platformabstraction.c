#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "platformabstraction.h"

// TODO, perhaps make versions of memcpy, so that I can put notes
// in of what to do

// TODO, put notes into all these functions, to give tips
// about how to make the assembly, and remove the
// reference to the syscalls

#define RAM_SIZE (10 * MEGABYTE)
BYTE random_access_memory[RAM_SIZE];

// byte of the begining of the frame
ADDRESS_OF_BYTE frame_pointer =
    (ADDRESS_OF_BYTE)(random_access_memory + RAM_SIZE);

void print_string(char *c) {
  /*
    For spim, in the data section at the top, give your string
    a name, and put the contents of the string in.
    This section comes before the .text section

        .data
nl:    .asciiz     "\n"
        .text
        .globl main

    ...
    ...
    ...
    when you are ready to print the string,
    do something like this, assuming your string name is "nl"

    li $v0, 4
    la $a0, nl
    syscall


   */
  printf("%s", c);
}

char *read_string() {
  /*
    For spim, do
    ...
   */
  // until I figure out what I want to do with this
  return NULL;
}

void print_int(int32_t i) {
  /*
    For spim, assuming your int start 8 bytes away from the
    stack pointer

    lw $a0, 8($sp)
    li $v0, 1
    syscall
  */
  printf("%d", i);
}

int32_t read_int() {
  /*
    For spim, do
    ...
  */
  int32_t toRead;
  scanf("%d", &toRead);
  return toRead;
}

void print_float(float f) {
  /*
    For spim, do
    ...
  */
  printf("%f", f);
}

float read_float() {

  /*
    For spim, do
    ...
  */

  float toRead;
  scanf("%f", &toRead);
  return toRead;
}

void xmemcpy(void *dest, void *src, size_t n) {
  /*
    For spim,

    ****if dest is relative to the frame pointer,****
    ****and src is a local variable in C, which is in a register
    ****in assembly
       do

          src  dest
           |    |
           |    |
           V    V

       sw $t0, x($fp)

       where x is the location of the variable in the frame
       is offset by x bytes.  Remember, 1 int is 4 bytes,
       1 pointer is 4 bytes, 1 character is 1 byte


    ****if dest is a local variable in C, which will be a register in assembly
    ****and src is is relative to the frame pointer
    ****in assembly



          dest src
           |    |
           |    |
           V    V

       lw $t0, x($fp)

       where x is the location of the variable in the frame
       is offset by x bytes.  Remember, 1 int is 4 bytes,
       1 pointer is 4 bytes, 1 character is 1 byte


    ...
  */

  memcpy(dest, src, n);
}
