/* SPIM S20 MIPS simulator.
   Definitions for the SPIM S20.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef SPIM_H
#define SPIM_H

/* This declaration must match the endianness of the machine SPIM is running on.
   You CANNOT set SPIM to simulate a different endianness than the machine that
   executes it.  meson.build auto-detects host endianness and defines
   SPIM_BIGENDIAN when needed; little-endian is the default (and matches every
   common host: x86, ARM, RISC-V, Apple Silicon). */

#ifndef SPIM_BIGENDIAN
#define SPIM_LITTLENDIAN
#endif

/* spim simulates a 32-bit MIPS, so register and memory words must be
   exactly 32 bits wide regardless of host word size.  Use the fixed-width
   integer types from <stdint.h>. */

#include <stdint.h>

typedef union {
  int i;
  void* p;
} intptr_union;

#define streq(s1, s2) !strcmp(s1, s2)

/* Round V to next greatest B boundary */
#define ROUND_UP(V, B) (((int)V + (B - 1)) & ~(B - 1))
#define ROUND_DOWN(V, B) (((int)V) & ~(B - 1))

/* Sign-extend the low 16 bits of X to a 32-bit signed integer.
   Casting through int16_t lets the compiler emit a single movsx. */
#define SIGN_EX(X) ((int32_t)(int16_t)(X))

#ifdef MIN /* Some systems define these in system includes */
#undef MIN
#endif
#ifdef MAX
#undef MAX
#endif
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

/* Useful and pervasive declarations: */

#include <stdlib.h>
#include <string.h>

#define K 1024

/* Type of a memory address.  Must be a 32-bit quantity to match MIPS.  */

typedef uint32_t mem_addr;

#define BYTES_PER_WORD 4 /* On the MIPS processor */

/* Sizes of memory segments. */

/* Initial size of text segment. */

#ifndef TEXT_SIZE
#define TEXT_SIZE (256 * K) /* 1/4 MB */
#endif

/* Initial size of k_text segment. */

#ifndef K_TEXT_SIZE
#define K_TEXT_SIZE (64 * K) /* 64 KB */
#endif

/* The data segment must be larger than 64K since we immediate grab
   64K for the small data segment pointed to by $gp. The data segment is
   expanded by an sbrk system call. */

/* Initial size of data segment. */

#ifndef DATA_SIZE
#define DATA_SIZE (256 * K) /* 1/4 MB */
#endif

/* Maximum size of data segment. */

#ifndef DATA_LIMIT
#define DATA_LIMIT (K * K) /* 1 MB */
#endif

/* Initial size of k_data segment. */

#ifndef K_DATA_SIZE
#define K_DATA_SIZE (64 * K) /* 64 KB */
#endif

/* Maximum size of k_data segment. */

#ifndef K_DATA_LIMIT
#define K_DATA_LIMIT (K * K) /* 1 MB */
#endif

/* The stack grows down automatically. */

/* Initial size of stack segment. */

#ifndef STACK_SIZE
#define STACK_SIZE (64 * K) /* 64 KB */
#endif

/* Maximum size of stack segment. */

#ifndef STACK_LIMIT
#define STACK_LIMIT (256 * K) /* 1/4 MB */
#endif

/* Name of the function to invoke at start up */

#define DEFAULT_RUN_LOCATION "__start"

/* Name of the symbol marking the end of the exception handler */

#define END_OF_TRAP_HANDLER_SYMBOL "__eoth"

/* Default number of instructions to execute. */

#define DEFAULT_RUN_STEPS 2147483647

/* Address to branch to when exception occurs */
#ifdef MIPS1
/* MIPS R2000 */
#define EXCEPTION_ADDR 0x80000080
#else
/* MIPS32 */
#define EXCEPTION_ADDR 0x80000180
#endif

/* Maximum size of object stored in the small data segment pointed to by $gp */

#define SMALL_DATA_SEG_MAX_SIZE 8

#ifndef DIRECT_MAPPED
#define DIRECT_MAPPED 0
#define TWO_WAY_SET 1
#endif

/* Interval (in instructions) at which memory-mapped IO registers are
   checked and updated. (This is to reduce overhead from making system calls
   to check for IO. It can be set as low as 1.) */

#define IO_INTERVAL 100

/* Number of IO_INTERVALs that a character remains in receiver buffer,
   even if another character is available. */

#define RECV_INTERVAL 100

/* Number of IO_INTERVALs that it takes to write a character. */

#define TRANS_LATENCY 100

/* Iterval (milliseconds) for the hardware timer in CP0. */

#define TIMER_TICK_MS 10 /* 100 times per second */

/* A port is either a Unix file descriptor (an int) or a FILE* pointer. */

#include <stdio.h>

typedef union {
  int i;
  FILE* f;
} port;

/* Exported functions (from spim.c or xspim.c): */

int console_input_available(void);
void error(char* fmt, ...);
[[noreturn]] void fatal_error(char* fmt, ...);
char get_console_char(void);
void put_console_char(char c);
int read_input(char* str, int n); /* Returns bytes read (0 on EOF). */
[[noreturn]] void run_error(char* fmt, ...);
void write_output(port, char* fmt, ...);

/* Exported variables: */

extern bool bare_machine;         /* => simulate bare machine */
extern bool accept_pseudo_insts;  /* => parse pseudo instructions  */
extern bool delayed_branches;     /* => simulate delayed branches */
extern bool delayed_loads;        /* => simulate delayed loads */
extern bool quiet;                /* => no warning messages */
extern char* exception_file_name; /* File containing exception handler */
extern bool force_break;          /* => stop interpreter loop  */
extern int spim_return_value;     /* Value returned when spim exits */
/* Actual type of structure pointed to depends on X/terminal interface */
extern port message_out, console_out, console_in;
extern bool mapped_io; /* => activate memory-mapped IO */
extern int initial_text_size;
extern int initial_data_size;
extern mem_addr initial_data_limit;
extern int initial_stack_size;
extern mem_addr initial_stack_limit;
extern int initial_k_text_size;
extern int initial_k_data_size;
extern mem_addr initial_k_data_limit;

#endif
