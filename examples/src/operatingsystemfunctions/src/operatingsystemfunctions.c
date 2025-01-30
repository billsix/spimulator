#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "operatingsystemfunctions.h"

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

ADDRESS_OF_BYTE stack_pointer =
    (ADDRESS_OF_BYTE)(random_access_memory + RAM_SIZE);

void operating_system_print_string(char *c) {
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

char *operating_system_read_string() {
  /*
    For spim, do
    ...
   */
  // until I figure out what I want to do with this
  return NULL;
}

char operating_system_read_char() {
  /*
    For spim,

    li $v0, 12
    syscall

    and then the chacter will be in $v0, which you should then put in RAM like

    sw $v0, 0($fp)

    the reason for saving it to RAM is so that you don't have to care about the
    how the value of $v0 changes over time, especially since it is used
    during system calls

    SPIM doesn't support EOF.  So set some other terminal input, such as "z"

    blt $t0, 'z', loopEnd


  */

  return getchar();
}

void operating_system_print_char(char c) {
  /*
    For spim, assuming that you char is in $t0

    li $v0, 11
    move $a0, $t0
    syscall

   */
  printf("%c", c);
}

void operating_system_print_int(int32_t i) {
  /*
    For spim, assuming your int start 8 bytes away from the
    stack pointer

    lw $a0, 8($sp)
    li $v0, 1
    syscall
  */
  printf("%d", i);
}

int32_t operating_system_read_int() {
  /*
    For spim, do
    ...
  */
  int32_t toRead;
  scanf("%d", &toRead);
  return toRead;
}

void operating_system_print_float(float f) {
  /*
    For spim, do
    ...
  */
  printf("%f", f);
}

float operating_system_read_float() {
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
