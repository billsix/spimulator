/* SPIM S20 MIPS simulator.
   Assemble-time event stream.

   The parser, action helpers (store_word / store_byte / record_label /
   etc.), and forward-reference resolution fire events through this
   interface as the assembler does its work.  An observer can subscribe
   and consume the event stream to produce a listing, drive a GUI,
   diff two parser implementations against each other, or assemble a
   teaching trace.

   Default observer is no-op; install one via assembler_set_observer().

   SPDX-License-Identifier: BSD-3-Clause */

#ifndef ASM_EVENT_H
#define ASM_EVENT_H

#include "spim.h"

typedef enum : uint8_t {
  AE_LABEL_DEF,        /* a label became defined at addr */
  AE_TEXT_INST,        /* an instruction was stored at addr (text seg) */
  AE_DATA_BYTE,        /* .byte EXPR */
  AE_DATA_HALF,        /* .half EXPR */
  AE_DATA_WORD,        /* .word EXPR */
  AE_DATA_DOUBLE,      /* .double EXPR */
  AE_DATA_STRING,      /* .ascii / .asciiz STRING */
  AE_ALIGN,            /* PC advanced for alignment */
  AE_SEG_CHANGE,       /* .text / .data / .ktext / .kdata */
  AE_FORWARD_REF,      /* instruction or word referenced an unresolved sym */
  AE_FORWARD_RESOLVED, /* forward reference got patched at label-def time */
} asm_event_kind;

typedef enum : uint8_t {
  ASM_SEG_TEXT,
  ASM_SEG_DATA,
  ASM_SEG_KTEXT,
  ASM_SEG_KDATA,
} asm_segment;

typedef struct asm_event {
  asm_event_kind kind;
  int source_line;         /* line_no at fire time; 0 if N/A */
  const char* source_file; /* input_file_name_get(); may be null */
  mem_addr address;           /* the memory address the event touches */
  union {
    struct {
      const char* name; /* label name (not owned) */
      bool global;
    } label_definition;

    struct {
      uint32_t encoding; /* 32-bit instruction word */
    } text_instruction;

    struct {
      int value; /* sign-extended for byte/half, raw for word */
    } data_integer;  /* used for AE_DATA_BYTE/HALF/WORD */

    struct {
      double value;
    } data_double;

    struct {
      const char* string; /* not owned; freed by the parser */
      int length;
      bool null_term;
    } data_string;

    struct {
      int new_program_counter_low_bits; /* alignment bits that got zeroed */
      int new_program_counter_offset;   /* bytes the PC was advanced */
      asm_segment segment;
    } align;

    struct {
      asm_segment segment;
    } segment_change;

    struct {
      const char* symbol; /* not owned */
      mem_addr from;      /* address of the use site */
    } forward_ref;

    struct {
      const char* symbol; /* not owned */
      mem_addr at;        /* the use-site address that got patched */
      int32_t value;      /* the resolved value written there */
    } forward_resolved;
  } payload;
} asm_event;

/* Observer signature.  The asm_event passed in is on the caller's
   stack and is only valid for the duration of the call — observers
   that need to retain the event should copy it. */
typedef void (*asm_event_observer)(const asm_event* e);

/* Install (or clear, with NULL) the global observer.  Returns the
   previous observer so callers can chain or restore. */
asm_event_observer assembler_set_observer(asm_event_observer observer);

/* Fire helpers — called by the action functions in instruction.c, data.c,
   sym-tbl.c.  Each is a thin wrapper that builds the event struct
   from the current line_no / input_file_name_get() and the per-kind
   payload, then calls the installed observer (no-op fast path if
   none). */
void assembler_fire_label_definition(mem_addr address, const char* name, bool global);
void assembler_fire_text_instruction(mem_addr address, uint32_t encoding);
void assembler_fire_data_byte(mem_addr address, int value);
void assembler_fire_data_half(mem_addr address, int value);
void assembler_fire_data_word(mem_addr address, int value);
void assembler_fire_data_double(mem_addr address, double value);
void assembler_fire_data_string(mem_addr address, const char* s, int length,
                          bool null_term);
void assembler_fire_align(mem_addr address, int low_bits, int offset, asm_segment segment);
void assembler_fire_segment_change(asm_segment segment, mem_addr address);
void assembler_fire_forward_reference(mem_addr from, const char* symbol);
void assembler_fire_forward_resolved(mem_addr at, const char* symbol, int32_t value);

#endif
