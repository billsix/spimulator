#ifndef __IOLIB_H
#define __IOLIB_H

void print_char(char c);
char read_char();


void print_int(int32_t i);
int32_t read_int();


// define CPU
#define x86_64_linux
//#define spim


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
ADDRESS_OF_BYTE frame_pointer = (ADDRESS_OF_BYTE)(random_access_memory + RAM_SIZE);

#endif
