/* SPIM S20 MIPS simulator.
   Macros for accessing memory.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef MEM_H
#define MEM_H

#include "spim.h"
#include "inst.h"
#include "reg.h"

/* A note on directions:  "Bottom" of memory is the direction of
   decreasing addresses.  "Top" is the direction of increasing addresses.*/

/* Type of contents of a memory word — 32 bits on MIPS regardless of host. */

typedef int32 /*@alt unsigned int @*/ mem_word;
static_assert(sizeof(mem_word) == BYTES_PER_WORD,
              "mem_word must be exactly one MIPS word wide");

/* The text segment and boundaries. */

extern instruction** text_seg;

extern bool text_modified; /* => text segment was written */

#define TEXT_BOT ((mem_addr)0x400000)

extern mem_addr text_top;

/* Amount to grow text segment when we run out of space for instructions. */

#define TEXT_CHUNK_SIZE 4096

/* The data segment and boundaries. */

extern mem_word* data_seg;

extern bool data_modified; /* => a data segment was written */

extern short* data_seg_h; /* Points to same vector as DATA_SEG */

#define BYTE_TYPE signed char

extern BYTE_TYPE* data_seg_b; /* Ditto */

#define DATA_BOT ((mem_addr)0x10000000)

extern mem_addr data_top;

extern mem_addr gp_midpoint; /* Middle of $gp area */

/* The stack segment and boundaries. */

extern mem_word* stack_seg;

extern short* stack_seg_h; /* Points to same vector as STACK_SEG */

extern BYTE_TYPE* stack_seg_b; /* Ditto */

extern mem_addr stack_bot;

/* Exclusive, but include 4K at top of stack. */

#define STACK_TOP ((mem_addr)0x80000000)

/* The kernel text segment and boundaries. */

extern instruction** k_text_seg;

#define K_TEXT_BOT ((mem_addr)0x80000000)

extern mem_addr k_text_top;

/* Kernel data segment and boundaries. */

extern mem_word* k_data_seg;

extern short* k_data_seg_h;

extern BYTE_TYPE* k_data_seg_b;

#define K_DATA_BOT ((mem_addr)0x90000000)

extern mem_addr k_data_top;

/* Memory-mapped IO area: */
#define MM_IO_BOT ((mem_addr)0xffff0000)
#define MM_IO_TOP ((mem_addr)0xffffffff)

/* Read from console: */
#define RECV_CTRL_ADDR ((mem_addr)0xffff0000)
#define RECV_BUFFER_ADDR ((mem_addr)0xffff0004)

#define RECV_READY 0x1
#define RECV_INT_ENABLE 0x2

#define RECV_INT_LEVEL 3 /* HW Interrupt 1 */

/* Write to console: */
#define TRANS_CTRL_ADDR ((mem_addr)0xffff0008)
#define TRANS_BUFFER_ADDR ((mem_addr)0xffff000c)

#define TRANS_READY 0x1
#define TRANS_INT_ENABLE 0x2

#define TRANS_INT_LEVEL 2 /* HW Interrupt 0 */

/* Exported functions: */

void check_memory_mapped_IO(void);
void expand_data(int addl_bytes);
void expand_k_data(int addl_bytes);
void expand_stack(int addl_bytes);
void make_memory(int text_size, int data_size, int data_limit, int stack_size,
                 int stack_limit, int k_text_size, int k_data_size,
                 int k_data_limit);
void* mem_reference(mem_addr addr);
void print_mem(mem_addr addr);
instruction* mem_read_inst(mem_addr addr);
reg_word mem_read_byte(mem_addr addr);
reg_word mem_read_half(mem_addr addr);
reg_word mem_read_word(mem_addr addr);
void mem_write_inst(mem_addr addr, instruction* inst);
void mem_write_byte(mem_addr addr, reg_word value);
void mem_write_half(mem_addr addr, reg_word value);
void mem_write_word(mem_addr addr, reg_word value);

#endif
