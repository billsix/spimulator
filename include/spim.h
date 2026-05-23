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
#include <stdlib.h>
#include <string.h>

typedef union {
  int i;
  void* p;
} intptr_union;

static inline bool streq(const char* a, const char* b) {
  return strcmp(a, b) == 0;
}

/* Sign-extend the low 16 bits of x to a 32-bit signed integer.
   Casting through int16_t lets the compiler emit a single movsx. */
static inline int32_t sign_ex(int x) {
  return (int32_t)(int16_t)x;
}

/* MIN/MAX/ROUND macros use C23-standardized typeof to capture each
   argument into a local before comparing, so the arguments are
   evaluated exactly once even when they have side effects.  Macros
   rather than functions because they need to be polymorphic across
   int / mem_addr / size_t / etc.  GNU statement-expression form
   (allowed by c_std=gnu23); the __extension__ keyword silences
   -Wpedantic's complaint about ISO C not allowing braced-groups in
   expressions. */

#ifdef MIN /* Some systems define these in system includes */
#undef MIN
#endif
#ifdef MAX
#undef MAX
#endif
#define MIN(A, B)                \
  __extension__ ({               \
    typeof(A) _spim_a_ = (A);    \
    typeof(B) _spim_b_ = (B);    \
    _spim_a_ < _spim_b_ ? _spim_a_ : _spim_b_; \
  })

#define MAX(A, B)                \
  __extension__ ({               \
    typeof(A) _spim_a_ = (A);    \
    typeof(B) _spim_b_ = (B);    \
    _spim_a_ > _spim_b_ ? _spim_a_ : _spim_b_; \
  })

/* Round V to next greatest B boundary (B must be a power of two). */
#define ROUND_UP(V, B)                                          \
  __extension__ ({                                              \
    typeof(V) _spim_v_ = (V);                                   \
    typeof(B) _spim_b_ = (B);                                   \
    (typeof(V))((_spim_v_ + _spim_b_ - 1) & ~(_spim_b_ - 1));   \
  })

#define ROUND_DOWN(V, B)                          \
  __extension__ ({                                \
    typeof(V) _spim_v_ = (V);                     \
    typeof(B) _spim_b_ = (B);                     \
    (typeof(V))(_spim_v_ & ~(_spim_b_ - 1));      \
  })

constexpr int K = 1024;

/* Type of a memory address.  Must be a 32-bit quantity to match MIPS.  */

typedef uint32_t mem_addr;

constexpr int BYTES_PER_WORD = 4; /* On the MIPS processor */

/* Sizes of memory segments. */

constexpr int TEXT_SIZE = 256 * K;   /* Initial text segment.  1/4 MB */
constexpr int K_TEXT_SIZE = 64 * K;  /* Initial k_text segment.  64 KB */

/* The data segment must be larger than 64K since we immediately grab
   64K for the small data segment pointed to by $gp. The data segment is
   expanded by an sbrk system call. */
constexpr int DATA_SIZE = 256 * K;      /* Initial data segment.  1/4 MB */
constexpr int DATA_LIMIT = K * K;       /* Max data segment.  1 MB */
constexpr int K_DATA_SIZE = 64 * K;     /* Initial k_data segment.  64 KB */
constexpr int K_DATA_LIMIT = K * K;     /* Max k_data segment.  1 MB */

/* The stack grows down automatically. */
constexpr int STACK_SIZE = 64 * K;      /* Initial stack segment.  64 KB */
constexpr int STACK_LIMIT = 256 * K;    /* Max stack segment.  1/4 MB */

/* Name of the function to invoke at start up */

#define DEFAULT_RUN_LOCATION "__start"

/* Name of the symbol marking the end of the exception handler */

#define END_OF_TRAP_HANDLER_SYMBOL "__eoth"

/* Default number of instructions to execute. */
constexpr int DEFAULT_RUN_STEPS = 2147483647;

/* Address to branch to when exception occurs.  Macro rather than
   constexpr so the #ifdef MIPS1 branch can select between values at
   preprocessing time. */
#ifdef MIPS1
/* MIPS R2000 */
#define EXCEPTION_ADDR 0x80000080
#else
/* MIPS32 */
#define EXCEPTION_ADDR 0x80000180
#endif

/* Maximum size of object stored in the small data segment pointed to by $gp */
constexpr int SMALL_DATA_SEG_MAX_SIZE = 8;

constexpr int DIRECT_MAPPED = 0;
constexpr int TWO_WAY_SET = 1;

/* Interval (in instructions) at which memory-mapped IO registers are
   checked and updated. (This is to reduce overhead from making system calls
   to check for IO. It can be set as low as 1.) */
constexpr int IO_INTERVAL = 100;

/* Number of IO_INTERVALs that a character remains in receiver buffer,
   even if another character is available. */
constexpr int RECV_INTERVAL = 100;

/* Number of IO_INTERVALs that it takes to write a character. */
constexpr int TRANS_LATENCY = 100;

/* Interval (milliseconds) for the hardware timer in CP0. */
constexpr int TIMER_TICK_MS = 10; /* 100 times per second */

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
