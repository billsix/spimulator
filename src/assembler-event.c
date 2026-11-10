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

asm_event_observer asm_set_observer(asm_event_observer obs) {
  asm_event_observer prev = current_observer;
  current_observer = obs;
  return prev;
}

/* Stamp the source position every event carries, then hand the event
   to the observer.  Callers check for a null observer first, so the
   no-observer fast path builds nothing. */
static void fire(asm_event e) {
  e.source_line = line_no;
  e.source_file = input_file_name_get();
  current_observer(&e);
}

void asm_fire_label_def(mem_addr addr, const char* name, bool global) {
  if (current_observer == nullptr) return;
  fire((asm_event){.kind = AE_LABEL_DEF,
                   .addr = addr,
                   .u.label_def = {.name = name, .global = global}});
}

void asm_fire_text_inst(mem_addr addr, uint32_t encoding) {
  if (current_observer == nullptr) return;
  fire((asm_event){.kind = AE_TEXT_INST,
                   .addr = addr,
                   .u.text_inst = {.encoding = encoding}});
}

void asm_fire_data_byte(mem_addr addr, int value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_BYTE, .addr = addr, .u.data_int = {.value = value}});
}

void asm_fire_data_half(mem_addr addr, int value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_HALF, .addr = addr, .u.data_int = {.value = value}});
}

void asm_fire_data_word(mem_addr addr, int value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_WORD, .addr = addr, .u.data_int = {.value = value}});
}

void asm_fire_data_double(mem_addr addr, double value) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_DOUBLE, .addr = addr, .u.data_double = {.value = value}});
}

void asm_fire_data_string(mem_addr addr, const char* s, int length,
                          bool null_term) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_DATA_STRING,
      .addr = addr,
      .u.data_string = {.string = s, .length = length, .null_term = null_term},
  });
}

void asm_fire_align(mem_addr addr, int low_bits, int offset, asm_segment seg) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_ALIGN,
      .addr = addr,
      .u.align = {.new_pc_low_bits = low_bits,
                  .new_pc_offset = offset,
                  .seg = seg},
  });
}

void asm_fire_seg_change(asm_segment seg, mem_addr addr) {
  if (current_observer == nullptr) return;
  fire((asm_event){
      .kind = AE_SEG_CHANGE, .addr = addr, .u.seg_change = {.seg = seg}});
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
