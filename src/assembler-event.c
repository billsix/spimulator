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

asm_event_observer asm_set_observer(asm_event_observer observer) {
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

void asm_fire_label_def(mem_addr address, const char* name, bool global) {
  if (current_observer == nullptr) return;
  fire((asm_event){.kind = AE_LABEL_DEF,
                   .addr = address,
                   .u.label_def = {.name = name, .global = global}});
}

void asm_fire_text_inst(mem_addr address, uint32_t encoding) {
  if (current_observer == nullptr) return;
  fire((asm_event){.kind = AE_TEXT_INST,
                   .addr = address,
                   .u.text_inst = {.encoding = encoding}});
}

void asm_fire_data_byte(mem_addr address, int value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_BYTE, .addr = address, .u.data_int = {.value = value}});
}

void asm_fire_data_half(mem_addr address, int value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_HALF, .addr = address, .u.data_int = {.value = value}});
}

void asm_fire_data_word(mem_addr address, int value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_WORD, .addr = address, .u.data_int = {.value = value}});
}

void asm_fire_data_double(mem_addr address, double value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_DOUBLE, .addr = address, .u.data_double = {.value = value}});
}

void asm_fire_data_string(mem_addr address, const char* s, int length,
                          bool null_term) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_STRING,
      .addr = address,
      .u.data_string = {.string = s, .length = length, .null_term = null_term},
  });
}

void asm_fire_align(mem_addr address, int low_bits, int offset, asm_segment segment) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_ALIGN,
      .addr = address,
      .u.align = {.new_pc_low_bits = low_bits,
                  .new_pc_offset = offset,
                  .seg = segment},
  });
}

void asm_fire_seg_change(asm_segment segment, mem_addr address) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_SEG_CHANGE, .addr = address, .u.seg_change = {.seg = segment}});
}

void asm_fire_forward_ref(mem_addr from, const char* symbol) {
  if (current_observer == nullptr) return;
  fire((asm_event){.kind = AE_FORWARD_REF,
                   .addr = from,
                   .u.forward_ref = {.symbol = symbol, .from = from}});
}

void asm_fire_forward_resolved(mem_addr at, const char* symbol, int32_t value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_FORWARD_RESOLVED,
      .addr = at,
      .u.forward_resolved = {.symbol = symbol, .at = at, .value = value},
  });
}
