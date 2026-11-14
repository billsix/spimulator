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
  ASSEMBLER_EVENT_LABEL_DEFINITION,        /* a label became defined at addr */
  ASSEMBLER_EVENT_TEXT_INSTRUCTION,        /* an instruction was stored at addr (text seg) */
  ASSEMBLER_EVENT_DATA_BYTE,        /* .byte EXPRESSION */
  ASSEMBLER_EVENT_DATA_HALF,        /* .half EXPRESSION */
  ASSEMBLER_EVENT_DATA_WORD,        /* .word EXPRESSION */
  ASSEMBLER_EVENT_DATA_DOUBLE,      /* .double EXPRESSION */
  ASSEMBLER_EVENT_DATA_STRING,      /* .ascii / .asciiz STRING */
  ASSEMBLER_EVENT_ALIGN,            /* PC advanced for alignment */
  ASSEMBLER_EVENT_SEGMENT_CHANGE,       /* .text / .data / .ktext / .kdata */
  ASSEMBLER_EVENT_FORWARD_REF,      /* instruction or word referenced an unresolved sym */
  ASSEMBLER_EVENT_FORWARD_RESOLVED, /* forward reference got patched at label-def time */
} assembler_event_kind;

typedef enum : uint8_t {
  ASSEMBLER_SEGMENT_TEXT,
  ASSEMBLER_SEGMENT_DATA,
  ASSEMBLER_SEGMENT_KERNEL_TEXT,
  ASSEMBLER_SEGMENT_KERNEL_DATA,
} assembler_segment;

typedef struct assembler_event {
  assembler_event_kind kind;
  int source_line;         /* line_no at fire time; 0 if N/A */
  const char* source_file; /* input_file_name_get(); may be null */
  memory_address address;           /* the memory address the event touches */
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
    } data_integer;  /* used for ASSEMBLER_EVENT_DATA_BYTE/HALF/WORD */

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
      assembler_segment segment;
    } align;

    struct {
      assembler_segment segment;
    } segment_change;

    struct {
      const char* symbol; /* not owned */
      memory_address from;      /* address of the use site */
    } forward_ref;

    struct {
      const char* symbol; /* not owned */
      memory_address at;        /* the use-site address that got patched */
      int32_t value;      /* the resolved value written there */
    } forward_resolved;
  } payload;
} assembler_event;

/* Observer signature.  The assembler_event passed in is on the caller's
   stack and is only valid for the duration of the call — observers
   that need to retain the event should copy it. */
typedef void (*assembler_event_observer)(const assembler_event* e);

/* Install (or clear, with NULL) the global observer.  Returns the
   previous observer so callers can chain or restore. */
assembler_event_observer assembler_set_observer(assembler_event_observer observer);

/* Fire helpers — called by the action functions in instruction.c, data.c,
   sym-tbl.c.  Each is a thin wrapper that builds the event struct
   from the current line_no / input_file_name_get() and the per-kind
   payload, then calls the installed observer (no-op fast path if
   none). */
void assembler_fire_label_definition(memory_address address, const char* name, bool global);
void assembler_fire_text_instruction(memory_address address, uint32_t encoding);
void assembler_fire_data_byte(memory_address address, int value);
void assembler_fire_data_half(memory_address address, int value);
void assembler_fire_data_word(memory_address address, int value);
void assembler_fire_data_double(memory_address address, double value);
void assembler_fire_data_string(memory_address address, const char* s, int length,
                          bool null_term);
void assembler_fire_align(memory_address address, int low_bits, int offset, assembler_segment segment);
void assembler_fire_segment_change(assembler_segment segment, memory_address address);
void assembler_fire_forward_reference(memory_address from, const char* symbol);
void assembler_fire_forward_resolved(memory_address at, const char* symbol, int32_t value);

#endif
