#ifndef __IOLIB_H
#define __IOLIB_H

char operating_system_read_char();
void operating_system_print_char(char c);

void operating_system_print_string(char* c);
char* operating_system_read_string();

void operating_system_print_int(int32_t i);
int32_t operating_system_read_int();

void xmemcpy(void* dest, void* src, size_t n);


// define CPU
#define x86_64_linux
//#define spim

#define SIZE_OF_CHAR 1

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
#define SIZE_OF_BYTE (sizeof(BYTE))
#define ADDRESS_OF_BYTE BYTE *

#define KILOBYTE 1024
#define MEGABYTE (KILOBYTE * KILOBYTE)


// define the Random Access Memory that will be available to our program
extern BYTE random_access_memory[];

// byte of the begining of the frame
extern ADDRESS_OF_BYTE frame_pointer;
extern ADDRESS_OF_BYTE stack_pointer;

#endif
