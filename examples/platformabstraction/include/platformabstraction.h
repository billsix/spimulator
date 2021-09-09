#ifndef __IOLIB_H
#define __IOLIB_H

void print_string(char* c);
char* read_string();

void print_int(int32_t i);
int32_t read_int();

void xmemcpy(void* dest, void* src, size_t n);


// define CPU
#define x86_64_linux
//#define spim


// define sizes based on the CPU
#ifdef x86_64_linux
#define SIZE_OF_INT32_T 4
#define SIZE_OF_ADDRESS_OF_BYTE 8
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
extern BYTE random_access_memory[];

// byte of the begining of the frame
extern ADDRESS_OF_BYTE frame_pointer;

#endif
