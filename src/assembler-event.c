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

static asm_event_observer current_observer = nullptr;

asm_event_observer assembler_set_observer(asm_event_observer observer) {
  const asm_event_observer previous = current_observer;
  current_observer = observer;
  return previous;
}

/* Stamp the source position every event carries, then hand the event
   to the observer.  Callers check for a null observer first, so the
   no-observer fast path builds nothing. */
static void fire(asm_event e) {
  e.source_line = line_no;
  e.source_file = input_file_name_get();
  current_observer(&e);
}

void assembler_fire_label_definition(mem_addr address, const char* name, bool global) {
  if (current_observer == nullptr) return;
  fire((asm_event){.kind = AE_LABEL_DEF,
                   .address = address,
                   .payload.label_definition = {.name = name, .global = global}});
}

void assembler_fire_text_instruction(mem_addr address, uint32_t encoding) {
  if (current_observer == nullptr) return;
  fire((asm_event){.kind = AE_TEXT_INST,
                   .address = address,
                   .payload.text_instruction = {.encoding = encoding}});
}

void assembler_fire_data_byte(mem_addr address, int value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_BYTE, .address = address, .payload.data_integer = {.value = value}});
}

void assembler_fire_data_half(mem_addr address, int value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_HALF, .address = address, .payload.data_integer = {.value = value}});
}

void assembler_fire_data_word(mem_addr address, int value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_WORD, .address = address, .payload.data_integer = {.value = value}});
}

void assembler_fire_data_double(mem_addr address, double value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_DOUBLE, .address = address, .payload.data_double = {.value = value}});
}

void assembler_fire_data_string(mem_addr address, const char* s, int length,
                          bool null_term) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_STRING,
      .address = address,
      .payload.data_string = {.string = s, .length = length, .null_term = null_term},
  });
}

void assembler_fire_align(mem_addr address, int low_bits, int offset, asm_segment segment) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_ALIGN,
      .address = address,
      .payload.align = {.new_program_counter_low_bits = low_bits,
                  .new_program_counter_offset = offset,
                  .segment = segment},
  });
}

void assembler_fire_segment_change(asm_segment segment, mem_addr address) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_SEG_CHANGE, .address = address, .payload.segment_change = {.segment = segment}});
}

void assembler_fire_forward_reference(mem_addr from, const char* symbol) {
  if (current_observer == nullptr) return;
  fire((asm_event){.kind = AE_FORWARD_REF,
                   .address = from,
                   .payload.forward_ref = {.symbol = symbol, .from = from}});
}

void assembler_fire_forward_resolved(mem_addr at, const char* symbol, int32_t value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_FORWARD_RESOLVED,
      .address = at,
      .payload.forward_resolved = {.symbol = symbol, .at = at, .value = value},
  });
}
