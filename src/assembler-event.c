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

/* Fill the common header fields.  Caller fills u.* and calls the
   observer.  We keep this inline-ish (just an extern call site) to
   keep the no-observer fast path tight. */
static inline void fill_header(asm_event* e, asm_event_kind k, mem_addr addr) {
  e->kind = k;
  e->source_line = line_no;
  e->source_file = input_file_name_get();
  e->addr = addr;
}

void asm_fire_label_def(mem_addr addr, const char* name, bool global) {
  if (current_observer == nullptr) return;
  asm_event e;
  fill_header(&e, AE_LABEL_DEF, addr);
  e.u.label_def.name = name;
  e.u.label_def.global = global;
  current_observer(&e);
}

void asm_fire_text_inst(mem_addr addr, uint32_t encoding) {
  if (current_observer == nullptr) return;
  asm_event e;
  fill_header(&e, AE_TEXT_INST, addr);
  e.u.text_inst.encoding = encoding;
  current_observer(&e);
}

void asm_fire_data_byte(mem_addr addr, int value) {
  if (current_observer == nullptr) return;
  asm_event e;
  fill_header(&e, AE_DATA_BYTE, addr);
  e.u.data_int.value = value;
  current_observer(&e);
}

void asm_fire_data_half(mem_addr addr, int value) {
  if (current_observer == nullptr) return;
  asm_event e;
  fill_header(&e, AE_DATA_HALF, addr);
  e.u.data_int.value = value;
  current_observer(&e);
}

void asm_fire_data_word(mem_addr addr, int value) {
  if (current_observer == nullptr) return;
  asm_event e;
  fill_header(&e, AE_DATA_WORD, addr);
  e.u.data_int.value = value;
  current_observer(&e);
}

void asm_fire_data_double(mem_addr addr, double value) {
  if (current_observer == nullptr) return;
  asm_event e;
  fill_header(&e, AE_DATA_DOUBLE, addr);
  e.u.data_double.value = value;
  current_observer(&e);
}

void asm_fire_data_string(mem_addr addr, const char* s, int length,
                          bool null_term) {
  if (current_observer == nullptr) return;
  asm_event e;
  fill_header(&e, AE_DATA_STRING, addr);
  e.u.data_string.string = s;
  e.u.data_string.length = length;
  e.u.data_string.null_term = null_term;
  current_observer(&e);
}

void asm_fire_align(mem_addr addr, int low_bits, int offset, asm_segment seg) {
  if (current_observer == nullptr) return;
  asm_event e;
  fill_header(&e, AE_ALIGN, addr);
  e.u.align.new_pc_low_bits = low_bits;
  e.u.align.new_pc_offset = offset;
  e.u.align.seg = seg;
  current_observer(&e);
}

void asm_fire_seg_change(asm_segment seg, mem_addr addr) {
  if (current_observer == nullptr) return;
  asm_event e;
  fill_header(&e, AE_SEG_CHANGE, addr);
  e.u.seg_change.seg = seg;
  current_observer(&e);
}

void asm_fire_forward_ref(mem_addr from, const char* symbol) {
  if (current_observer == nullptr) return;
  asm_event e;
  fill_header(&e, AE_FORWARD_REF, from);
  e.u.forward_ref.from = from;
  e.u.forward_ref.symbol = symbol;
  current_observer(&e);
}

void asm_fire_forward_resolved(mem_addr at, const char* symbol, int32_t value) {
  if (current_observer == nullptr) return;
  asm_event e;
  fill_header(&e, AE_FORWARD_RESOLVED, at);
  e.u.forward_resolved.at = at;
  e.u.forward_resolved.symbol = symbol;
  e.u.forward_resolved.value = value;
  current_observer(&e);
}
