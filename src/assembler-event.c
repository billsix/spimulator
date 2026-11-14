/* SPIM S20 MIPS simulator.
   Assemble-time event stream — observer slot and fire helpers.

   The observer is a single global function pointer.  Callers fire
   events through asm_fire_* helpers; the helpers populate the event
   struct (including source-line context from the scanner + parser)
   and invoke the observer.  When no observer is installed, the
   helpers are a single null-check.

   SPDX-License-Identifier: BSD-3-Clause */

#include "assembler-event.h"
#include "scanner.h"
#include "parser.h"

static assembler_event_observer current_observer = nullptr;

assembler_event_observer assembler_set_observer(
    assembler_event_observer observer) {
  const assembler_event_observer previous = current_observer;
  current_observer = observer;
  return previous;
}

/* Stamp the source position every event carries, then hand the event
   to the observer.  Callers check for a null observer first, so the
   no-observer fast path builds nothing. */
static void fire(assembler_event e) {
  e.source_line = line_no;
  e.source_file = input_file_name_get();
  current_observer(&e);
}

void assembler_fire_label_definition(memory_address address, const char* name,
                                     bool global) {
  if (current_observer == nullptr) return;
  fire((assembler_event){
      .kind = ASSEMBLER_EVENT_LABEL_DEFINITION,
      .address = address,
      .payload.label_definition = {.name = name, .global = global}});
}

void assembler_fire_text_instruction(memory_address address,
                                     uint32_t encoding) {
  if (current_observer == nullptr) return;
  fire((assembler_event){.kind = ASSEMBLER_EVENT_TEXT_INSTRUCTION,
                         .address = address,
                         .payload.text_instruction = {.encoding = encoding}});
}

void assembler_fire_data_byte(memory_address address, int value) {
  if (current_observer == nullptr) return;
  fire((assembler_event){.kind = ASSEMBLER_EVENT_DATA_BYTE,
                         .address = address,
                         .payload.data_integer = {.value = value}});
}

void assembler_fire_data_half(memory_address address, int value) {
  if (current_observer == nullptr) return;
  fire((assembler_event){.kind = ASSEMBLER_EVENT_DATA_HALF,
                         .address = address,
                         .payload.data_integer = {.value = value}});
}

void assembler_fire_data_word(memory_address address, int value) {
  if (current_observer == nullptr) return;
  fire((assembler_event){.kind = ASSEMBLER_EVENT_DATA_WORD,
                         .address = address,
                         .payload.data_integer = {.value = value}});
}

void assembler_fire_data_double(memory_address address, double value) {
  if (current_observer == nullptr) return;
  fire((assembler_event){.kind = ASSEMBLER_EVENT_DATA_DOUBLE,
                         .address = address,
                         .payload.data_double = {.value = value}});
}

void assembler_fire_data_string(memory_address address, const char* s,
                                int length, bool null_term) {
  if (current_observer == nullptr) return;
  fire((assembler_event){
      .kind = ASSEMBLER_EVENT_DATA_STRING,
      .address = address,
      .payload.data_string = {.string = s,
                              .length = length,
                              .null_term = null_term},
  });
}

void assembler_fire_align(memory_address address, int low_bits, int offset,
                          assembler_segment segment) {
  if (current_observer == nullptr) return;
  fire((assembler_event){
      .kind = ASSEMBLER_EVENT_ALIGN,
      .address = address,
      .payload.align = {.new_program_counter_low_bits = low_bits,
                        .new_program_counter_offset = offset,
                        .segment = segment},
  });
}

void assembler_fire_segment_change(assembler_segment segment,
                                   memory_address address) {
  if (current_observer == nullptr) return;
  fire((assembler_event){.kind = ASSEMBLER_EVENT_SEGMENT_CHANGE,
                         .address = address,
                         .payload.segment_change = {.segment = segment}});
}

void assembler_fire_forward_reference(memory_address from, const char* symbol) {
  if (current_observer == nullptr) return;
  fire((assembler_event){
      .kind = ASSEMBLER_EVENT_FORWARD_REF,
      .address = from,
      .payload.forward_ref = {.symbol = symbol, .from = from}});
}

void assembler_fire_forward_resolved(memory_address at, const char* symbol,
                                     int32_t value) {
  if (current_observer == nullptr) return;
  fire((assembler_event){
      .kind = ASSEMBLER_EVENT_FORWARD_RESOLVED,
      .address = at,
      .payload.forward_resolved = {.symbol = symbol, .at = at, .value = value},
  });
}
