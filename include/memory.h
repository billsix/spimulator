/* SPIM S20 MIPS simulator.
   Macros for accessing memory.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifndef MEM_H
#define MEM_H

#include "spim.h"
#include "instruction.h"
#include "registers.h"

/* A note on directions:  "Bottom" of memory is the direction of
   decreasing addresses.  "Top" is the direction of increasing addresses.*/

/* Type of contents of a memory word — 32 bits on MIPS regardless of host. */

typedef int32_t memory_word;
static_assert(sizeof(memory_word) == BYTES_PER_WORD,
              "mem_word must be exactly one MIPS word wide");

/* The text segment and boundaries. */

extern mips_instruction** text_segment;

extern bool text_modified; /* => text segment was written */

constexpr memory_address TEXT_BOTTOM = 0x400000;

extern memory_address text_top;

/* Amount to grow text segment when we run out of space for instructions. */
constexpr int TEXT_CHUNK_SIZE = 4096;

/* The data segment and boundaries. */

extern memory_word* data_segment;

extern bool data_modified; /* => a data segment was written */

extern short* data_segment_halfwords; /* Points to same vector as DATA_SEG */

extern int8_t* data_segment_bytes; /* Ditto */

constexpr memory_address DATA_BOTTOM = 0x10000000;

extern memory_address data_top;

extern memory_address global_pointer_midpoint; /* Middle of $gp area */

/* The stack segment and boundaries. */

extern memory_word* stack_segment;

extern short* stack_segment_halfwords; /* Points to same vector as STACK_SEG */

extern int8_t* stack_segment_bytes; /* Ditto */

extern memory_address stack_bottom;

/* Exclusive, but include 4K at top of stack. */
constexpr memory_address STACK_TOP = 0x80000000;

/* The kernel text segment and boundaries. */

extern mips_instruction** kernel_text_segment;

constexpr memory_address KERNEL_TEXT_BOTTOM = 0x80000000;

extern memory_address kernel_text_top;

/* Kernel data segment and boundaries. */

extern memory_word* kernel_data_segment;

extern short* kernel_data_segment_halfwords;

extern int8_t* kernel_data_segment_bytes;

constexpr memory_address KERNEL_DATA_BOTTOM = 0x90000000;

extern memory_address kernel_data_top;

/* Memory-mapped IO area: */
constexpr memory_address MEMORY_MAPPED_IO_BOTTOM = 0xffff0000;
constexpr memory_address MEMORY_MAPPED_IO_TOP = 0xffffffff;

/* Read from console: */
constexpr memory_address RECEIVER_CONTROL_ADDRESS = 0xffff0000;
constexpr memory_address RECEIVER_BUFFER_ADDRESS = 0xffff0004;

constexpr uint32_t RECEIVER_READY = 0x1;
constexpr uint32_t RECEIVER_INTERRUPT_ENABLE = 0x2;

constexpr int RECEIVER_INTERRUPT_LEVEL = 3; /* HW Interrupt 1 */

/* Write to console: */
constexpr memory_address TRANSMITTER_CONTROL_ADDRESS = 0xffff0008;
constexpr memory_address TRANSMITTER_BUFFER_ADDRESS = 0xffff000c;

constexpr uint32_t TRANSMITTER_READY = 0x1;
constexpr uint32_t TRANSMITTER_INTERRUPT_ENABLE = 0x2;

constexpr int TRANSMITTER_INTERRUPT_LEVEL = 2; /* HW Interrupt 0 */

/* Exported functions: */

void check_memory_mapped_IO(void);
void expand_data(int additional_bytes);
void expand_kernel_data(int additional_bytes);
void expand_stack(int additional_bytes);
void make_memory(int text_size, int data_size, int data_limit, int stack_size,
                 int stack_limit, int kernel_text_size, int kernel_data_size,
                 int kernel_data_limit);
void* memory_reference(memory_address address);
void print_memory(memory_address address);
mips_instruction* memory_read_instruction(memory_address address);
register_word memory_read_byte(memory_address address);
register_word memory_read_half(memory_address address);
register_word memory_read_word(memory_address address);
/* Side-effect-free reads, for code that only shows memory (the teaching
   mode, the disassembler).  They raise no exception, grow no segment and
   touch no memory-mapped IO register.  Each returns false when ADDR is not
   an aligned location in a segment; memory_peek_instruction then leaves *OUT alone,
   and memory_peek sets *OUT to 0.  MASK is 0, 1 or 3 for a byte, half or word. */
[[nodiscard]] bool memory_peek_instruction(memory_address address, mips_instruction** out);
bool memory_peek(memory_address address, int mask, register_word* out);
void memory_write_instruction(memory_address address, mips_instruction* instruction);
void memory_write_byte(memory_address address, register_word value);
void memory_write_half(memory_address address, register_word value);
void memory_write_word(memory_address address, register_word value);

#endif
