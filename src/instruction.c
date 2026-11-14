
/* SPIM S20 MIPS simulator.
   Code to build assembly instructions and resolve symbolic labels.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#include <stdio.h>
#include <string.h>

#include "spim.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "instruction.h"
#include "registers.h"
#include "memory.h"
#include "symbol-table.h"
#include "parser.h"
#include "scanner.h"
#include "tokens.h"
#include "opcode-types.h"
#include "opcode-tables.h"
#include "data.h"
#include "assembler-event.h"

/* Local functions: */

static immediate_expression* copy_immediate_expression(const immediate_expression* old_expression);
static void increment_text_location(int delta);
static immediate_expression* lower_bits_of_expression(const immediate_expression* old_expression);
static void store_instruction(mips_instruction* instruction);
static immediate_expression* upper_bits_of_expression(const immediate_expression* old_expression);

static void format_immediate_expression(string_stream* stream, immediate_expression* expression, int base_register);
static void i_type_full_word(int opcode, int target_register, int source_register, const immediate_expression* expression,
                             bool value_known, int32_t value);
static void compare_instructions(mips_instruction* instruction1, mips_instruction* instruction2);
static mips_instruction* make_r_type(int opcode, int destination_register, int source_register, int target_register);
static mips_instruction* make_i_instruction(int32_t value, int opcode, int source_register, int target_register,
                                   int offset);
static mips_instruction* make_j_instruction(int32_t value, int opcode, int target);
static mips_instruction* make_r_instruction(int32_t value, int opcode, int source_register, int target_register,
                                   int destination_register, int shift_amount);
static mips_instruction* make_coprocessor_r_instruction(int32_t value, int opcode, int floating_point_destination, int floating_point_source,
                                      int floating_point_target);
static void produce_immediate(const immediate_expression* expression, int target_register, bool value_known,
                              int32_t value);

/* Local variables: */

/* True means store instructions in kernel, not user, text segment */

static bool in_kernel = false;

/* Instruction used as breakpoint by SPIM: */

static mips_instruction* break_instruction = nullptr;

/* Locations for next instruction in user and kernel text segments */

static memory_address next_text_location;

static memory_address next_kernel_text_location;

#define INSTRUCTION_PROGRAM_COUNTER (in_kernel ? next_kernel_text_location : next_text_location)

/* Set ADDRESS at which the next instruction is stored. */

void text_begins_at_point(memory_address address) {
  next_text_location = address;
  in_kernel = false;
}

void kernel_text_begins_at_point(memory_address address) {
  next_kernel_text_location = address;
  in_kernel = false;
}

/* Arrange that the next instruction is stored on a memory boundary with its
   low ALIGNMENT bits equal to 0.  An ALIGNMENT of 0 does nothing in text:
   .align 0 turns off the automatic alignment of .half, .word, .float and
   .double, which applies only in the data segments (set_data_alignment), and
   instructions are always word-aligned. */

void align_text(int alignment) {
  if (alignment == 0) {
    /* Nothing to turn off in text (see above). */
  } else if (in_kernel) {
    next_kernel_text_location =
        (next_kernel_text_location + (1u << alignment) - 1) & (0xffffffff << alignment);
    fix_current_label_address(next_kernel_text_location);
  } else {
    next_text_location =
        (next_text_location + (1u << alignment) - 1) & (0xffffffff << alignment);
    fix_current_label_address(next_text_location);
  }
}

/* Set the location (in user or kernel text space) for the next instruction. */

void set_text_location(memory_address address) {
  if (in_kernel)
    next_kernel_text_location = address;
  else
    next_text_location = address;
}

/* Return address for next instruction, in appropriate text segment. */

memory_address current_text_location(void) { return (INSTRUCTION_PROGRAM_COUNTER); }

/* Increment the current text segement PC. */

static void increment_text_location(int delta) {
  if (in_kernel) {
    next_kernel_text_location += (memory_address)delta;
    if (kernel_text_top <= next_kernel_text_location)
      run_error("Can't expand kernel text segment\n");
  } else {
    next_text_location += (memory_address)delta;
    if (text_top <= next_text_location) run_error("Can't expand text segment\n");
  }
}

/* If FLAG is true, next instruction goes to kernel text segment,
   otherwise it goes to user segment. */

void user_kernel_text_segment(bool to_kernel) { in_kernel = to_kernel; }

/* Source-annotation override for deferred (AST) emission.
   store_instruction records each instruction's originating source line
   for the listing/explain annotation.  During a normal parse the
   scanner is still on that line, so source_line() reads it live; but
   emit_ast replays the tree after the parse has finished, when the
   scanner has moved to the last line of the file.  So emit_ast wraps
   each node's emission in emit_source_set(node->source_text) /
   emit_source_clear(): while active, store_instruction copies the
   supplied text instead of the stale live line.  When inactive (e.g.
   direct emission with the scanner still current), it falls back to
   source_line(). */
static bool emit_source_active = false;
static const char* emit_source_text = nullptr;

void emit_source_set(const char* s) {
  emit_source_active = true;
  emit_source_text = s;
}

void emit_source_clear(void) {
  emit_source_active = false;
  emit_source_text = nullptr;
}

/* The instructions stored for the source instruction being emitted, for
   expansion_end.  An expansion longer than the array (none is) is still
   recorded for its first EXPANSION_MAX instructions. */
constexpr int EXPANSION_MAX = 16;
static int expansion_depth = 0;
static const char* expansion_mnemonic = nullptr;
static bool expansion_is_pseudo = false;
static mips_instruction* expansion_instructions[EXPANSION_MAX];
static int expansion_length = 0;

void expansion_begin(const char* mnemonic, bool is_pseudo) {
  if (expansion_depth++ > 0) return;
  /* The mnemonic's own string may be freed with its AST; keep the
     opcode table's copy. */
  const name_value_value* const entry =
      map_string_to_name_value_value(opcode_table, OPCODE_COUNT, mnemonic);
  expansion_mnemonic = entry != nullptr ? entry->name : nullptr;
  expansion_is_pseudo = is_pseudo;
  expansion_length = 0;
}

void expansion_end(void) {
  if (--expansion_depth > 0) return;
  if (expansion_mnemonic != nullptr &&
      (expansion_is_pseudo || expansion_length > 1)) {
    for (int i = 0; i < expansion_length; i++) {
      expansion_instructions[i]->expansion_of = expansion_mnemonic;
      expansion_instructions[i]->expansion_index = (uint8_t)(i + 1);
      expansion_instructions[i]->expansion_count = (uint8_t)expansion_length;
      expansion_instructions[i]->expansion_of_pseudo = expansion_is_pseudo;
    }
  } else {
    /* Written as itself: the instruction keeps no expansion record. */
  }
  expansion_length = 0;
}

/* Store an INSTRUCTION in memory at the next location. */

static void store_instruction(mips_instruction* instruction) {
  if (data_directive) {
    store_word(instruction_encode(instruction));
    free_instruction(instruction);
  } else if (text_directive) {
    const memory_address at = INSTRUCTION_PROGRAM_COUNTER;
    exception_occurred = false;
    memory_write_instruction(at, instruction);
    if (exception_occurred)
      error("Invalid address (0x%08x) for instruction\n", at);
    else
      increment_text_location(BYTES_PER_WORD);
    if (instruction != nullptr) {
      SET_SOURCE(instruction,
                 emit_source_active
                     ? (emit_source_text ? strdup(emit_source_text) : nullptr)
                     : source_line());
      if (ENCODING(instruction) == 0)
        SET_ENCODING(instruction, instruction_encode(instruction));
      if (expansion_depth > 0 && expansion_length < EXPANSION_MAX)
        expansion_instructions[expansion_length++] = instruction;
      assembler_fire_text_instruction(at, (uint32_t)ENCODING(instruction));
    }
  } else {
    /* Never reached: the parser always sets data_directive and text_directive as a pair,
       one of them true. */
    fatal_error(
        "internal error: store_instruction: neither the data nor the text "
        "segment is active\n");
  }
}

void i_type_free(int opcode, int target_register, int source_register, immediate_expression* expression) {
  i_type(opcode, target_register, source_register, expression);
  free(expression);
}

/* Produce an immediate instruction with the OPCODE, RT, RS, and IMMEDIATE
   fields.  NB, because the immediate value may not fit in the field,
   this routine may produce more than one instruction.	On the bare
   machine, we resolve symbolic address, but they better produce values
   that fit into instruction's immediate field. */

void i_type(int opcode, int target_register, int source_register, const immediate_expression* expression) {
  mips_instruction* instruction =
      (mips_instruction*)allocate_zeroed_or_exit(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_SOURCE_REGISTER(instruction, source_register);
  SET_TARGET_REGISTER(instruction, target_register);
  SET_EXPRESSION(instruction, copy_immediate_expression(expression));
  if (expression->symbol == nullptr || SYMBOL_IS_DEFINED(expression->symbol)) {
    /* Evaluate the instruction's expression. */
    const int32_t value = evaluate_immediate_expression(expression);

    if (!bare_machine &&
        (((opcode == TOKEN_ADDI_OPCODE || opcode == TOKEN_ADDIU_OPCODE ||
           opcode == TOKEN_SLTI_OPCODE || opcode == TOKEN_SLTIU_OPCODE ||
           opcode == TOKEN_TEQI_OPCODE || opcode == TOKEN_TGEI_OPCODE ||
           opcode == TOKEN_TGEIU_OPCODE || opcode == TOKEN_TLTI_OPCODE ||
           opcode == TOKEN_TLTIU_OPCODE || opcode == TOKEN_TNEI_OPCODE ||
           (opcode_is_load_store(opcode) && expression->bits == 0))
              // Sign-extended immediate values:
              ? (((unsigned int)value & 0xffff8000) != 0 &&
                 ((unsigned int)value & 0xffff8000) != 0xffff8000)
              // Not sign-extended:
              : ((unsigned int)value & 0xffff0000) != 0))) {
      // Non-immediate value
      free_instruction(instruction);
      i_type_full_word(opcode, target_register, source_register, expression, true, value);
      return;
    } else
      resolve_a_label(expression->symbol, instruction);
  } else if (bare_machine || expression->bits != 0)
    /* Don't know expression's value, but only needed upper/lower 16-bits
       anyways. */
    record_instruction_uses_symbol(instruction, expression->symbol);
  else {
    /* Don't know the expressions's value and want all of its bits,
       so assume that it will not produce a small result and generate
       sequence for 32 bit value. */
    free_instruction(instruction);

    i_type_full_word(opcode, target_register, source_register, expression, false, 0);
    return;
  }

  store_instruction(instruction);
}

/* The immediate value for an instruction will (or may) not fit in 16 bits.
   Build the value from its piece with separate instructions. */

static void i_type_full_word(int opcode, int target_register, int source_register, const immediate_expression* expression,
                             bool value_known, int32_t value) {
  if (opcode_is_load_store(opcode)) {
    int32_t offset;

    if (expression->symbol != nullptr && expression->symbol->global_pointer_flag && source_register == 0 &&
        IMMEDIATE_MIN <= (offset = (int32_t)(expression->symbol->address + expression->offset)) &&
        offset <= IMMEDIATE_MAX) {
      i_type_free(opcode, target_register, GLOBAL_POINTER_REGISTER, make_immediate_expression(offset, nullptr, false));
    } else if (value_known) {
      int high = (value >> 16) & 0xffff;
      const int low = value & 0xffff;

      if (!(high == 0 && !(low & 0x8000)) &&
          !(high == 0xffff && (low & 0x8000))) {
        /* Some of high 16 bits are non-zero */
        if (low & 0x8000) {
          /* Adjust high 16, since load sign-extends low 16*/
          high += 1;
        }

        i_type_free(TOKEN_LUI_OPCODE, 1, 0, constant_immediate_expression(high));
        if (source_register != 0) /* Base register */
        {
          r_type(TOKEN_ADDU_OPCODE, 1, 1, source_register);
        }
        /* lower_bits_of_expression copies its argument, so the constant_immediate_expression
           node it wraps is not adopted by anyone — free the intermediate. */
        immediate_expression* const low_expression = constant_immediate_expression(low);
        i_type_free(opcode, target_register, 1, lower_bits_of_expression(low_expression));
        free(low_expression);
      } else {
        /* Special case, sign-extension of low 16 bits sets high to 0xffff */
        i_type_free(opcode, target_register, source_register, constant_immediate_expression(low));
      }
    } else {
      /* Use $at */
      /* Need to adjust if lower bits are negative */
      i_type_free(TOKEN_LUI_OPCODE, 1, 0, upper_bits_of_expression(expression));
      if (source_register != 0) /* Base register */
      {
        r_type(TOKEN_ADDU_OPCODE, 1, 1, source_register);
      }
      i_type_free(opcode, target_register, 1, lower_bits_of_expression(expression));
    }
  } else if (opcode_is_branch(opcode)) {
    /* This only allows branches +/- 32K, which is not correct! */
    i_type_free(opcode, target_register, source_register, lower_bits_of_expression(expression));
  } else
  /* Computation instruction */
  {
    int offset;

    if (expression->symbol != nullptr && expression->symbol->global_pointer_flag && source_register == 0 &&
        IMMEDIATE_MIN <= (offset = (int32_t)(expression->symbol->address + expression->offset)) &&
        offset <= IMMEDIATE_MAX) {
      i_type_free((opcode == TOKEN_LUI_OPCODE ? TOKEN_ADDIU_OPCODE : opcode), target_register,
                  GLOBAL_POINTER_REGISTER, make_immediate_expression(offset, nullptr, false));
    } else {
      /* Use $at */
      if ((opcode == TOKEN_ORI_OPCODE || opcode == TOKEN_ADDI_OPCODE ||
           opcode == TOKEN_ADDIU_OPCODE || opcode == TOKEN_LUI_OPCODE) &&
          source_register == 0) {
        produce_immediate(expression, target_register, value_known, value);
      } else {
        produce_immediate(expression, 1, value_known, value);
        r_type(immediate_opcode_to_opcode(opcode), target_register, source_register, 1);
      }
    }
  }
}

static void produce_immediate(const immediate_expression* expression, int target_register, bool value_known,
                              int32_t value) {
  if (value_known && (value & 0xffff) == 0) {
    i_type_free(TOKEN_LUI_OPCODE, target_register, 0, upper_bits_of_expression(expression));
  } else if (value_known && ((unsigned int)value & 0xffff0000) == 0) {
    i_type_free(TOKEN_ORI_OPCODE, target_register, 0, lower_bits_of_expression(expression));
  } else {
    i_type_free(TOKEN_LUI_OPCODE, 1, 0, upper_bits_of_expression(expression));
    i_type_free(TOKEN_ORI_OPCODE, target_register, 1, lower_bits_of_expression(expression));
  }
}

/* Return a jump-type instruction with the given OPCODE and TARGET
   fields. NB, even the immediate value may not fit in the field, this
   routine will not produce more than one instruction. */

void j_type(int opcode, immediate_expression* target) {
  mips_instruction* instruction =
      (mips_instruction*)allocate_zeroed_or_exit(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  target->offset = 0; /* Not PC relative */
  target->program_counter_relative = false;
  SET_EXPRESSION(instruction, copy_immediate_expression(target));
  if (target->symbol == nullptr || SYMBOL_IS_DEFINED(target->symbol))
    resolve_a_label(target->symbol, instruction);
  else
    record_instruction_uses_symbol(instruction, target->symbol);
  store_instruction(instruction);
}

/* Return a register-type instruction with the given OPCODE, RD, RS, and RT
   fields. */

static mips_instruction* make_r_type(int opcode, int destination_register, int source_register, int target_register) {
  mips_instruction* instruction =
      (mips_instruction*)allocate_zeroed_or_exit(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_SOURCE_REGISTER(instruction, source_register);
  SET_TARGET_REGISTER(instruction, target_register);
  SET_DESTINATION_REGISTER(instruction, destination_register);
  return (instruction);
}

/* Return a register-type instruction with the given OPCODE, RD, RS, and RT
   fields. */

void r_type(int opcode, int destination_register, int source_register, int target_register) {
  store_instruction(make_r_type(opcode, destination_register, source_register, target_register));
}

/* Return a register-type instruction with the given OPCODE, FD, FS, and FT
   fields. */

void r_coprocessor_type(int opcode, int floating_point_destination, int floating_point_source, int floating_point_target) {
  mips_instruction* instruction = make_r_type(opcode, floating_point_source, 0, floating_point_target);
  SET_FLOATING_POINT_DESTINATION(instruction, floating_point_destination);
  store_instruction(instruction);
}

/* Return a register-shift instruction with the given OPCODE, RD, RT, and
   SHIFT_AMOUNT fields.*/

void r_shift_amount_type(int opcode, int destination_register, int target_register, int shift_amount) {
  mips_instruction* instruction = make_r_type(opcode, destination_register, 0, target_register);
  SET_SHIFT_AMOUNT(instruction, shift_amount & 0x1f);
  store_instruction(instruction);
}

/* Return a floating-point compare instruction with the given OPCODE,
   FS, FT, and CC fields.*/

void r_condition_type(int opcode, int floating_point_source, int floating_point_target, int condition_code) {
  mips_instruction* instruction = make_r_type(opcode, floating_point_source, 0, floating_point_target);
  SET_FLOATING_POINT_DESTINATION(instruction, condition_code << 2);
  switch (opcode) {
    case TOKEN_C_EQ_D_OPCODE:
    case TOKEN_C_EQ_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_EQUAL);
      break;
    }

    case TOKEN_C_LE_D_OPCODE:
    case TOKEN_C_LE_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_INVALID | CONDITION_LESS_THAN | CONDITION_EQUAL);
      break;
    }

    case TOKEN_C_LT_D_OPCODE:
    case TOKEN_C_LT_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_INVALID | CONDITION_LESS_THAN);
      break;
    }

    case TOKEN_C_NGE_D_OPCODE:
    case TOKEN_C_NGE_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_INVALID | CONDITION_LESS_THAN | CONDITION_UNORDERED);
      break;
    }

    case TOKEN_C_NGLE_D_OPCODE:
    case TOKEN_C_NGLE_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_INVALID | CONDITION_UNORDERED);
      break;
    }

    case TOKEN_C_NGL_D_OPCODE:
    case TOKEN_C_NGL_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_INVALID | CONDITION_EQUAL | CONDITION_UNORDERED);
      break;
    }

    case TOKEN_C_NGT_D_OPCODE:
    case TOKEN_C_NGT_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_INVALID | CONDITION_LESS_THAN | CONDITION_EQUAL | CONDITION_UNORDERED);
      break;
    }

    case TOKEN_C_OLT_D_OPCODE:
    case TOKEN_C_OLT_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_LESS_THAN);
      break;
    }

    case TOKEN_C_OLE_D_OPCODE:
    case TOKEN_C_OLE_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_LESS_THAN | CONDITION_EQUAL);
      break;
    }

    case TOKEN_C_SEQ_D_OPCODE:
    case TOKEN_C_SEQ_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_INVALID | CONDITION_EQUAL);
      break;
    }

    case TOKEN_C_SF_D_OPCODE:
    case TOKEN_C_SF_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_INVALID);
      break;
    }

    case TOKEN_C_F_D_OPCODE:
    case TOKEN_C_F_S_OPCODE: {
      SET_CONDITION(instruction, 0);
      break;
    }

    case TOKEN_C_UEQ_D_OPCODE:
    case TOKEN_C_UEQ_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_EQUAL | CONDITION_UNORDERED);
      break;
    }

    case TOKEN_C_ULT_D_OPCODE:
    case TOKEN_C_ULT_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_LESS_THAN | CONDITION_UNORDERED);
      break;
    }

    case TOKEN_C_ULE_D_OPCODE:
    case TOKEN_C_ULE_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_LESS_THAN | CONDITION_EQUAL | CONDITION_UNORDERED);
      break;
    }

    case TOKEN_C_UN_D_OPCODE:
    case TOKEN_C_UN_S_OPCODE: {
      SET_CONDITION(instruction, CONDITION_UNORDERED);
      break;
    }
    default:
      /* Never reached: only FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION rows reach here, and the cases
         above are all 32 of them (16 conditions, .s and .d). */
      fatal_error("internal error: r_cond_type: not an FP compare: %d\n",
                  opcode);
  }
  store_instruction(instruction);
}

/* Make and return a deep copy of INST. */

mips_instruction* copy_instruction(const mips_instruction* instruction) {
  mips_instruction* new_instruction =
      (mips_instruction*)allocate_or_exit(sizeof(mips_instruction));

  *new_instruction = *instruction;
  /*memcpy ((void*)new_instruction, (void*)instruction , sizeof (instruction));*/
  SET_EXPRESSION(new_instruction, copy_immediate_expression(EXPRESSION(instruction)));
  return (new_instruction);
}

void free_instruction(mips_instruction* instruction) {
  if (instruction != break_instruction)
  /* Don't free the breakpoint insructions since we only have one. */
  {
    if (EXPRESSION(instruction)) free(EXPRESSION(instruction));
    free(instruction);
  }
}

/* The opcode tables (opcode_table, opcode_encoding_table, opcode_decode_table) are
   generated from include/opcodes.tsv into src/opcode-tables.c, already in
   the order map_integer_to_name_value_value's bsearch needs; see opcode-tables.h. */

/* Print the instruction stored at the memory ADDRESS. */

void print_instruction(memory_address address) {
  char* const instruction_string = instruction_to_string(address);
  write_output(message_out, "%s", instruction_string);
  free(instruction_string);
}

/* Return the canonical mnemonic for instruction (e.g. "addu", "ori", "j"), or
   "<unknown>" if OPCODE(instruction) isn't in opcode_table. The returned pointer
   is owned by the static opcode_table and must not be freed. */
const char* instruction_opcode_name(const mips_instruction* instruction) {
  if (instruction == nullptr) return "<null>";
  const name_value_value* const entry =
      map_integer_to_name_value_value(opcode_table, OPCODE_COUNT, OPCODE(instruction));
  return entry ? entry->name : "<unknown>";
}

/* Op-token variant: take a raw opcode token (TOK_*_OP, TOK_*_POP) and
   return its mnemonic — used by the AST builder to label AST_PSEUDO
   nodes. */
const char* opcode_token_name(int opcode_token) {
  const name_value_value* const entry =
      map_integer_to_name_value_value(opcode_table, OPCODE_COUNT, opcode_token);
  return entry ? entry->name : "<unknown>";
}

char* instruction_to_string(memory_address address) {
  /* A side-effect-free read: showing an instruction must not raise, or
     clear, the exception the program's own step raised. */
  mips_instruction* instruction = nullptr;
  if (!memory_peek_instruction(address, &instruction)) {
    error("Can't print instruction not in text segment (0x%08x)\n", address);
    /* Callers free the result, so it must be heap-allocated — a string
       literal here would get free()d. */
    return strdup("");
  }

  string_stream stream;
  stream_initialize(&stream);
  format_an_instruction(&stream, instruction, address);
  /* Transfer the buffer to the caller (who frees it); ss is a local,
     so its stream must not outlive this call. */
  return stream_take_string(&stream);
}

void format_an_instruction(string_stream* stream, mips_instruction* instruction,
                    memory_address address) {
  const name_value_value* entry;
  const int line_start = stream_length(stream);

  if (address != 0 && instruction_is_breakpoint(address)) {
    delete_breakpoint(address);
    stream_printf(stream, "*");
    format_an_instruction(stream, memory_read_instruction(address), address);
    add_breakpoint(address);
    return;
  }

  stream_printf(stream, "[0x%08x]\t", address);
  if (instruction == nullptr) {
    stream_printf(stream, "<none>\n");
    return;
  }

  entry =
      map_integer_to_name_value_value(opcode_table, OPCODE_COUNT, OPCODE(instruction));
  if (entry == nullptr) {
    stream_printf(stream, "<unknown instruction %d>\n", OPCODE(instruction));
    return;
  }

  stream_printf(stream, "0x%08x  %s", (uint32_t)ENCODING(instruction), entry->name);
  switch (entry->value2) {
    case BRANCH_COPROCESSOR_CONDITION_INSTRUCTION:
      stream_printf(stream, "%d %d", CONDITION_CODE(instruction), (int)BRANCH_OFFSET(instruction));
      break;

    case BRANCH_SOURCE_REGISTER_LABEL_INSTRUCTION:
      stream_printf(stream, " $%s %d", integer_register_names[SOURCE_REGISTER(instruction)],
                (int)BRANCH_OFFSET(instruction));
      break;

    case SOURCE_REGISTER_IMMEDIATE_INSTRUCTION:
      stream_printf(stream, " $%s, %d", integer_register_names[SOURCE_REGISTER(instruction)],
                IMMEDIATE(instruction));
      break;

    case TARGET_REGISTER_IMMEDIATE_INSTRUCTION:
      stream_printf(stream, " $%s, %d", integer_register_names[TARGET_REGISTER(instruction)],
                IMMEDIATE(instruction));
      break;

    case TARGET_REGISTER_SOURCE_REGISTER_IMMEDIATE_INSTRUCTION:
      stream_printf(stream, " $%s, $%s, %d", integer_register_names[TARGET_REGISTER(instruction)],
                integer_register_names[SOURCE_REGISTER(instruction)], IMMEDIATE(instruction));
      break;

    case BRANCH_SOURCE_REGISTER_TARGET_REGISTER_LABEL_INSTRUCTION:
      stream_printf(stream, " $%s, $%s, %d", integer_register_names[SOURCE_REGISTER(instruction)],
                integer_register_names[TARGET_REGISTER(instruction)],
                (int)BRANCH_OFFSET(instruction));
      break;

    case TARGET_REGISTER_ADDRESS_INSTRUCTION:
      stream_printf(stream, " $%s, %d($%s)", integer_register_names[TARGET_REGISTER(instruction)],
                IMMEDIATE(instruction), integer_register_names[BASE(instruction)]);
      break;

    case SOURCE_REGISTER_INSTRUCTION:
      stream_printf(stream, " $%s", integer_register_names[SOURCE_REGISTER(instruction)]);
      break;

    case DESTINATION_REGISTER_INSTRUCTION:
      stream_printf(stream, " $%s", integer_register_names[DESTINATION_REGISTER(instruction)]);
      break;

    case TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION:
      stream_printf(stream, " $%s, $%s", integer_register_names[TARGET_REGISTER(instruction)],
                integer_register_names[DESTINATION_REGISTER(instruction)]);
      break;

    case SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION:
      stream_printf(stream, " $%s, $%s", integer_register_names[SOURCE_REGISTER(instruction)],
                integer_register_names[TARGET_REGISTER(instruction)]);
      break;

    case DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION:
      stream_printf(stream, " $%s, $%s", integer_register_names[DESTINATION_REGISTER(instruction)],
                integer_register_names[SOURCE_REGISTER(instruction)]);
      break;

    case DESTINATION_REGISTER_TARGET_REGISTER_SHIFT_AMOUNT_INSTRUCTION:
      /* nop is canonically sll $0, $0, 0. ENCODING() is unreliable here
       * (the assembler doesn't populate it for source-assembled instructions,
       * so it's always 0 and would falsely make every shift print as nop).
       * Check the actual fields instead. */
      if (DESTINATION_REGISTER(instruction) == 0 && TARGET_REGISTER(instruction) == 0 &&
          SHIFT_AMOUNT(instruction) == 0) {
        stream_erase(stream, 3); /* zap sll */
        stream_printf(stream, "nop");
      } else
        stream_printf(stream, " $%s, $%s, %d", integer_register_names[DESTINATION_REGISTER(instruction)],
                  integer_register_names[TARGET_REGISTER(instruction)], SHIFT_AMOUNT(instruction));
      break;

    case DESTINATION_REGISTER_SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION:
      stream_printf(stream, " $%s, $%s, $%s", integer_register_names[DESTINATION_REGISTER(instruction)],
                integer_register_names[SOURCE_REGISTER(instruction)], integer_register_names[TARGET_REGISTER(instruction)]);
      break;

    case DESTINATION_REGISTER_TARGET_REGISTER_SOURCE_REGISTER_INSTRUCTION:
      stream_printf(stream, " $%s, $%s, $%s", integer_register_names[DESTINATION_REGISTER(instruction)],
                integer_register_names[TARGET_REGISTER(instruction)], integer_register_names[SOURCE_REGISTER(instruction)]);
      break;

    case FLOATING_POINT_TARGET_ADDRESS_INSTRUCTION:
      stream_printf(stream, " $f%d, %d($%s)", FLOATING_POINT_TARGET(instruction), IMMEDIATE(instruction),
                integer_register_names[BASE(instruction)]);
      break;

    case FLOATING_POINT_DESTINATION_SOURCE_INSTRUCTION:
      stream_printf(stream, " $f%d, $f%d", FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_SOURCE(instruction));
      break;

    case FLOATING_POINT_TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION:
      stream_printf(stream, " $%s, $f%d", integer_register_names[TARGET_REGISTER(instruction)],
                FLOATING_POINT_SOURCE(instruction));
      break;

    case FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION:
      if (FLOATING_POINT_DESTINATION(instruction) == 0)
        stream_printf(stream, " $f%d, $f%d", FLOATING_POINT_SOURCE(instruction), FLOATING_POINT_TARGET(instruction));
      else
        stream_printf(stream, " %d, $f%d, $f%d", FLOATING_POINT_DESTINATION(instruction) >> 2, FLOATING_POINT_SOURCE(instruction),
                  FLOATING_POINT_TARGET(instruction));
      break;

    case FLOATING_POINT_DESTINATION_SOURCE_TARGET_INSTRUCTION:
      stream_printf(stream, " $f%d, $f%d, $f%d", FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_SOURCE(instruction),
                FLOATING_POINT_TARGET(instruction));
      break;

    case MOVE_CONDITIONAL_TYPE_INSTRUCTION:
      stream_printf(stream, " $%s, $%s, %d", integer_register_names[DESTINATION_REGISTER(instruction)],
                integer_register_names[SOURCE_REGISTER(instruction)], TARGET_REGISTER(instruction) >> 2);
      break;

    case FLOATING_POINT_MOVE_CONDITIONAL_TYPE_INSTRUCTION:
      switch (OPCODE(instruction)) {
        case TOKEN_MOVN_S_OPCODE:
        case TOKEN_MOVN_D_OPCODE:
        case TOKEN_MOVZ_S_OPCODE:
        case TOKEN_MOVZ_D_OPCODE:
          /* movn/movz test a general register */
          stream_printf(stream, " $f%d, $f%d, $%s", FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_SOURCE(instruction),
                    integer_register_names[TARGET_REGISTER(instruction)]);
          break;
        default:
          /* movf/movt test a condition code */
          stream_printf(stream, " $f%d, $f%d, %d", FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_SOURCE(instruction),
                    CONDITION_CODE(instruction));
          break;
      }
      break;

    case J_TYPE_INSTRUCTION:
      stream_printf(stream, " 0x%08x", TARGET(instruction) << 2);
      break;

    case NO_ARGUMENT_TYPE_INSTRUCTION:
      break;

    default:
      fatal_error("Unknown instruction type in print_inst\n");
  }

  if (EXPRESSION(instruction) != nullptr && EXPRESSION(instruction)->symbol != nullptr) {
    stream_printf(stream, " [");
    if (opcode_is_load_store(OPCODE(instruction)))
      format_immediate_expression(stream, EXPRESSION(instruction), BASE(instruction));
    else
      format_immediate_expression(stream, EXPRESSION(instruction), -1);
    stream_printf(stream, "]");
  }

  if (SOURCE(instruction) != nullptr) {
    /* Comment is source line text of current line. */
    int gap_length = 57 - (stream_length(stream) - line_start);
    for (; 0 < gap_length; gap_length -= 1) {
      stream_printf(stream, " ");
    }

    stream_printf(stream, "; ");
    stream_printf(stream, "%s", SOURCE(instruction));
  }

  stream_printf(stream, "\n");
}

/* Return true if SPIM OPCODE (e.g. Y_...) represents a conditional
   branch. */

bool opcode_is_branch(int opcode) {
  switch (opcode) {
    case TOKEN_BC1F_OPCODE:
    case TOKEN_BC1FL_OPCODE:
    case TOKEN_BC1T_OPCODE:
    case TOKEN_BC1TL_OPCODE:
    case TOKEN_BC2F_OPCODE:
    case TOKEN_BC2FL_OPCODE:
    case TOKEN_BC2T_OPCODE:
    case TOKEN_BC2TL_OPCODE:
    case TOKEN_BEQ_OPCODE:
    case TOKEN_BEQL_OPCODE:
    case TOKEN_BEQZ_PSEUDO_OP:
    case TOKEN_BGE_PSEUDO_OP:
    case TOKEN_BGEU_PSEUDO_OP:
    case TOKEN_BGEZ_OPCODE:
    case TOKEN_BGEZAL_OPCODE:
    case TOKEN_BGEZALL_OPCODE:
    case TOKEN_BGEZL_OPCODE:
    case TOKEN_BGT_PSEUDO_OP:
    case TOKEN_BGTU_PSEUDO_OP:
    case TOKEN_BGTZ_OPCODE:
    case TOKEN_BGTZL_OPCODE:
    case TOKEN_BLE_PSEUDO_OP:
    case TOKEN_BLEU_PSEUDO_OP:
    case TOKEN_BLEZ_OPCODE:
    case TOKEN_BLEZL_OPCODE:
    case TOKEN_BLT_PSEUDO_OP:
    case TOKEN_BLTU_PSEUDO_OP:
    case TOKEN_BLTZ_OPCODE:
    case TOKEN_BLTZAL_OPCODE:
    case TOKEN_BLTZALL_OPCODE:
    case TOKEN_BLTZL_OPCODE:
    case TOKEN_BNE_OPCODE:
    case TOKEN_BNEL_OPCODE:
    case TOKEN_BNEZ_PSEUDO_OP:
      return true;

    default:
      return false;
  }
}

/* Return true if SPIM OPCODE represents a nullified (e.g., Y_...L_OP)
   conditional branch. */

bool opcode_is_nullified_branch(int opcode) {
  switch (opcode) {
    case TOKEN_BC1FL_OPCODE:
    case TOKEN_BC1TL_OPCODE:
    case TOKEN_BC2FL_OPCODE:
    case TOKEN_BC2TL_OPCODE:
    case TOKEN_BEQL_OPCODE:
    case TOKEN_BGEZALL_OPCODE:
    case TOKEN_BGEZL_OPCODE:
    case TOKEN_BGTZL_OPCODE:
    case TOKEN_BLEZL_OPCODE:
    case TOKEN_BLTZALL_OPCODE:
    case TOKEN_BLTZL_OPCODE:
    case TOKEN_BNEL_OPCODE:
      return true;

    default:
      return false;
  }
}

/* Return true if SPIM OPCODE (e.g. Y_...) represents a conditional
   branch on a true condition. */

bool opcode_is_true_branch(int opcode) {
  switch (opcode) {
    case TOKEN_BC1T_OPCODE:
    case TOKEN_BC1TL_OPCODE:
    case TOKEN_BC2T_OPCODE:
    case TOKEN_BC2TL_OPCODE:
      return true;

    default:
      return false;
  }
}

/* Return true if SPIM OPCODE (e.g. Y_...) is a direct unconditional
   branch (jump). */

bool opcode_is_jump(int opcode) {
  switch (opcode) {
    case TOKEN_J_OPCODE:
    case TOKEN_JAL_OPCODE:
      return true;

    default:
      return false;
  }
}

/* Return true if SPIM OPCODE (e.g. Y_...) is a load or store. */

bool opcode_is_load_store(int opcode) {
  switch (opcode) {
    case TOKEN_LB_OPCODE:
    case TOKEN_LBU_OPCODE:
    case TOKEN_LH_OPCODE:
    case TOKEN_LHU_OPCODE:
    case TOKEN_LL_OPCODE:
    case TOKEN_LDC1_OPCODE:
    case TOKEN_LDC2_OPCODE:
    case TOKEN_LW_OPCODE:
    case TOKEN_LWC1_OPCODE:
    case TOKEN_LWC2_OPCODE:
    case TOKEN_LWL_OPCODE:
    case TOKEN_LWR_OPCODE:
    case TOKEN_SB_OPCODE:
    case TOKEN_SC_OPCODE:
    case TOKEN_SH_OPCODE:
    case TOKEN_SDC1_OPCODE:
    case TOKEN_SDC2_OPCODE:
    case TOKEN_SW_OPCODE:
    case TOKEN_SWC1_OPCODE:
    case TOKEN_SWC2_OPCODE:
    case TOKEN_SWL_OPCODE:
    case TOKEN_SWR_OPCODE:
      return true;

    default:
      return false;
  }
}

/* Return true if a breakpoint is set at ADDR. */

bool instruction_is_breakpoint(memory_address address) {
  if (break_instruction == nullptr)
    break_instruction = make_r_type(TOKEN_BREAK_OPCODE, 1, 0, 0);

  return (memory_read_instruction(address) == break_instruction);
}

/* Set a breakpoint at ADDR and return the old instruction.  If the
   breakpoint cannot be set, return nullptr. */

mips_instruction* set_breakpoint(memory_address address) {
  if (break_instruction == nullptr)
    break_instruction = make_r_type(TOKEN_BREAK_OPCODE, 1, 0, 0);

  exception_occurred = false;
  mips_instruction* const old_instruction = memory_read_instruction(address);
  if (old_instruction == break_instruction) return (nullptr);

  memory_write_instruction(address, break_instruction);
  if (exception_occurred)
    return (nullptr);
  else
    return (old_instruction);
}

/* An immediate expression has the form: SYMBOL +/- IMMEDIATE_OFFSET, where either
   part may be omitted. */

/* Make and return a new immediate expression */

immediate_expression* make_immediate_expression(int offset, const char* symbol, bool is_program_counter_relative) {
  immediate_expression* expression = (immediate_expression*)allocate_or_exit(sizeof(immediate_expression));

  *expression = (immediate_expression){
      .offset = offset,
      .symbol = symbol != nullptr ? lookup_label(symbol) : nullptr,
      .program_counter_relative = is_program_counter_relative,
  };
  return (expression);
}

/* Return a shallow copy of the EXPRESSION. */

static immediate_expression* copy_immediate_expression(const immediate_expression* old_expression) {
  immediate_expression* expression = (immediate_expression*)allocate_or_exit(sizeof(immediate_expression));

  *expression = *old_expression;
  /*memcpy ((void*)expr, (void*)old_expression, sizeof (immediate_expression));*/
  return (expression);
}

/* Return a shallow copy of an EXPRESSION that only uses the upper
   sixteen bits of the expression's value. */

static immediate_expression* upper_bits_of_expression(const immediate_expression* old_expression) {
  immediate_expression* expression = copy_immediate_expression(old_expression);

  expression->bits = 1;
  return (expression);
}

/* Return a shallow copy of the EXPRESSION that only uses the lower
   sixteen bits of the expression's value. */

static immediate_expression* lower_bits_of_expression(const immediate_expression* old_expression) {
  immediate_expression* expression = copy_immediate_expression(old_expression);

  expression->bits = -1;
  return (expression);
}

/* Return an instruction expression for a constant VALUE. */

immediate_expression* constant_immediate_expression(int32_t value) {
  return (make_immediate_expression(value, nullptr, false));
}

/* Return a shallow copy of the EXPRESSION with the offset field
   incremented by the given amount. */

immediate_expression* increment_expression_offset(const immediate_expression* expression, int32_t value) {
  immediate_expression* new_expression = copy_immediate_expression(expression);

  new_expression->offset += value;
  return (new_expression);
}

/* Return the value of the EXPRESSION. */

int32_t evaluate_immediate_expression(const immediate_expression* expression) {
  int32_t value;

  if (expression->symbol == nullptr)
    value = expression->offset;
  else if (SYMBOL_IS_DEFINED(expression->symbol)) {
    value = (int32_t)(expression->offset + expression->symbol->address);
  } else {
    error("Evaluated undefined symbol: %s\n", expression->symbol->name);
    value = 0;
  }
  if (expression->bits > 0)
    return ((value >> 16) & 0xffff); /* Use upper bits of result */
  else if (expression->bits < 0)
    return (value & 0xffff); /* Use lower bits */
  else
    return (value);
}

/* Print the EXPRESSION. */

static void format_immediate_expression(string_stream* stream, immediate_expression* expression, int base_register) {
  if (expression->symbol != nullptr) {
    stream_printf(stream, "%s", expression->symbol->name);
  }

  if (expression->program_counter_relative)
    stream_printf(stream, "-0x%08x", (unsigned int)-expression->offset);
  else if (expression->offset < -10)
    stream_printf(stream, "-%d (-0x%08x)", -expression->offset, (unsigned int)-expression->offset);
  else if (expression->offset > 10)
    stream_printf(stream, "+%d (0x%08x)", expression->offset, (unsigned int)expression->offset);
  else {
    /* An offset from -10 to 10 is not printed. */
  }

  if (base_register != -1 && expression->symbol != nullptr &&
      (expression->offset > 10 || expression->offset < -10)) {
    if (expression->offset == 0 && base_register != 0) stream_printf(stream, "+0");

    if (expression->offset != 0 || base_register != 0)
      stream_printf(stream, "($%s)", integer_register_names[base_register]);
  }
}

/* Return true if the EXPRESSION is a constant 0. */

bool is_zero_immediate(const immediate_expression* expression) {
  return (expression->offset == 0 && expression->symbol == nullptr);
}

/* Return an address expression of the form SYMBOL +/- IMMEDIATE_OFFSET (REGISTER).
   Any of the three parts may be omitted. */

address_expression* make_address_expression(int offset, const char* symbol, int register_no) {
  address_expression* expression = (address_expression*)allocate_or_exit(sizeof(address_expression));
  label* looked_up;

  if (register_no == 0 && symbol != nullptr &&
      (looked_up = lookup_label(symbol))->global_pointer_flag) {
    *expression = (address_expression){
        .register_no = GLOBAL_POINTER_REGISTER,
        .immediate = make_immediate_expression((int)(offset + looked_up->address - global_pointer_midpoint),
                             nullptr, false),
    };
  } else {
    /* make_immediate_expression only reads `sym` (via lookup_label) and stores the
       resulting label*, never the string itself — so it neither retains
       nor frees it.  strdup'ing here produced a copy that was orphaned on
       every call; pass the caller's `sym` straight through instead.  The
       caller keeps ownership and frees it. */
    *expression = (address_expression){
        .register_no = (uint8_t)register_no,
        .immediate = make_immediate_expression(offset, symbol, false),
    };
  }
  return (expression);
}

immediate_expression* address_expression_immediate(const address_expression* expression) { return (expression->immediate); }

int address_expression_register(const address_expression* expression) { return (expression->register_no); }

/* Map between a SPIM instruction and the binary representation of the
   instruction. */

#define REGISTERS(R, O) (((R) & 0x1f) << O)

int32_t instruction_encode(const mips_instruction* instruction) {
  if (instruction == nullptr) return (0);

  const name_value_value* entry = map_integer_to_name_value_value(
      opcode_encoding_table, OPCODE_COUNT, OPCODE(instruction));
  if (entry == nullptr) return 0;

  const int32_t table_opcode = entry->value2;
  entry =
      map_integer_to_name_value_value(opcode_table, OPCODE_COUNT, OPCODE(instruction));

  switch (entry->value2) {
    case BRANCH_COPROCESSOR_CONDITION_INSTRUCTION:
      return (table_opcode | REGISTERS(CONDITION_CODE(instruction) << 2, 16) |
              (IMMEDIATE_OFFSET(instruction) & 0xffff));

    case BRANCH_SOURCE_REGISTER_LABEL_INSTRUCTION:
      return (table_opcode | REGISTERS(SOURCE_REGISTER(instruction), 21) |
              (IMMEDIATE_OFFSET(instruction) & 0xffff));

    case SOURCE_REGISTER_IMMEDIATE_INSTRUCTION:
      return (table_opcode | REGISTERS(SOURCE_REGISTER(instruction), 21) |
              (IMMEDIATE(instruction) & 0xffff));

    case TARGET_REGISTER_IMMEDIATE_INSTRUCTION:
      return (table_opcode | REGISTERS(SOURCE_REGISTER(instruction), 21) | REGISTERS(TARGET_REGISTER(instruction), 16) |
              (IMMEDIATE(instruction) & 0xffff));

    case TARGET_REGISTER_SOURCE_REGISTER_IMMEDIATE_INSTRUCTION:
    case BRANCH_SOURCE_REGISTER_TARGET_REGISTER_LABEL_INSTRUCTION:
      return (table_opcode | REGISTERS(SOURCE_REGISTER(instruction), 21) | REGISTERS(TARGET_REGISTER(instruction), 16) |
              (IMMEDIATE(instruction) & 0xffff));

    case TARGET_REGISTER_ADDRESS_INSTRUCTION:
      return (table_opcode | REGISTERS(BASE(instruction), 21) |
              REGISTERS(TARGET_REGISTER(instruction), 16) | (IMMEDIATE_OFFSET(instruction) & 0xffff));

    case SOURCE_REGISTER_INSTRUCTION:
      return (table_opcode | REGISTERS(SOURCE_REGISTER(instruction), 21));

    case DESTINATION_REGISTER_INSTRUCTION:
      return (table_opcode | REGISTERS(DESTINATION_REGISTER(instruction), 11));

    case TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION:
      return (table_opcode | REGISTERS(TARGET_REGISTER(instruction), 16) | REGISTERS(DESTINATION_REGISTER(instruction), 11));

    case SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION:
      return (table_opcode | REGISTERS(SOURCE_REGISTER(instruction), 21) | REGISTERS(TARGET_REGISTER(instruction), 16));

    case DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION:
      return (table_opcode | REGISTERS(SOURCE_REGISTER(instruction), 21) | REGISTERS(DESTINATION_REGISTER(instruction), 11));

    case DESTINATION_REGISTER_TARGET_REGISTER_SHIFT_AMOUNT_INSTRUCTION:
      return (table_opcode | REGISTERS(TARGET_REGISTER(instruction), 16) | REGISTERS(DESTINATION_REGISTER(instruction), 11) |
              REGISTERS(SHIFT_AMOUNT(instruction), 6));

    case DESTINATION_REGISTER_SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION:
      return (table_opcode | REGISTERS(SOURCE_REGISTER(instruction), 21) | REGISTERS(TARGET_REGISTER(instruction), 16) |
              REGISTERS(DESTINATION_REGISTER(instruction), 11));

    case DESTINATION_REGISTER_TARGET_REGISTER_SOURCE_REGISTER_INSTRUCTION:
      return (table_opcode | REGISTERS(SOURCE_REGISTER(instruction), 21) | REGISTERS(TARGET_REGISTER(instruction), 16) |
              REGISTERS(DESTINATION_REGISTER(instruction), 11));

    case FLOATING_POINT_TARGET_ADDRESS_INSTRUCTION:
      return (table_opcode | REGISTERS(BASE(instruction), 21) |
              REGISTERS(TARGET_REGISTER(instruction), 16) | (IMMEDIATE_OFFSET(instruction) & 0xffff));

    case FLOATING_POINT_DESTINATION_SOURCE_INSTRUCTION:
      return (table_opcode | REGISTERS(FLOATING_POINT_SOURCE(instruction), 11) | REGISTERS(FLOATING_POINT_DESTINATION(instruction), 6));

    case FLOATING_POINT_TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION:
      return (table_opcode | REGISTERS(TARGET_REGISTER(instruction), 16) | REGISTERS(FLOATING_POINT_SOURCE(instruction), 11));

    case FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION:
      return (table_opcode | REGISTERS(FLOATING_POINT_TARGET(instruction), 16) | REGISTERS(FLOATING_POINT_SOURCE(instruction), 11) |
              REGISTERS(FLOATING_POINT_DESTINATION(instruction), 6) | CONDITION(instruction));

    case FLOATING_POINT_DESTINATION_SOURCE_TARGET_INSTRUCTION:
      return (table_opcode | REGISTERS(FLOATING_POINT_TARGET(instruction), 16) | REGISTERS(FLOATING_POINT_SOURCE(instruction), 11) |
              REGISTERS(FLOATING_POINT_DESTINATION(instruction), 6));

    case MOVE_CONDITIONAL_TYPE_INSTRUCTION:
      return (table_opcode | REGISTERS(SOURCE_REGISTER(instruction), 21) | REGISTERS(TARGET_REGISTER(instruction), 16) |
              REGISTERS(DESTINATION_REGISTER(instruction), 11));

    case FLOATING_POINT_MOVE_CONDITIONAL_TYPE_INSTRUCTION:
      /* The whole rt field: a GPR for movn/movz, cc<<2 for movf/movt (whose tf
         bit is already in table_opcode). */
      return (table_opcode | REGISTERS(TARGET_REGISTER(instruction), 16) | REGISTERS(FLOATING_POINT_SOURCE(instruction), 11) |
              REGISTERS(FLOATING_POINT_DESTINATION(instruction), 6));

    case J_TYPE_INSTRUCTION:
      return ((int32_t)((memory_address)table_opcode | TARGET(instruction)));

    case NO_ARGUMENT_TYPE_INSTRUCTION:
      return (table_opcode);

    default:
      fatal_error("Unknown instruction type in inst_encoding\n");
  }
}

mips_instruction* instruction_decode(int32_t value) {
  int32_t table_opcode = (int32_t)((unsigned int)value & 0xfc000000);

  /* Field classes: (opcode is continued in other part of instruction): */
  switch (table_opcode) {
    case 0:          /* SPECIAL */
    case 0x70000000: /* SPECIAL2 */
      table_opcode |= (value & 0x3f);
      break;
    case 0x04000000: /* REGIMM */
      table_opcode |= (value & 0x001f0000);
      break;
    case 0x40000000: /* COP0 */
      table_opcode |= (value & 0x03e00000) | (value & 0x1f);
      break;
    case 0x44000000: /* COP1 */
      table_opcode |= (value & 0x03e00000);
      if (((unsigned int)value & 0xff000000) == 0x45000000)
        table_opcode |= (value & 0x00010000); /* BC1f/t */
      else
        table_opcode |= (value & 0x3f);
      break;
    case 0x48000000: /* COP2: the rs field */
      table_opcode |= (value & 0x03e00000);
      break;
    case 0x4c000000: /* COP1X: the funct field */
      table_opcode |= (value & 0x3f);
      break;
    default:
      /* Any other primary opcode is the whole key: nothing to add. */
      break;
  }

  const name_value_value* const entry =
      map_integer_to_name_value_value(opcode_decode_table, OPCODE_DECODE_COUNT, table_opcode);
  if (entry == nullptr)
    return (make_r_instruction(value, 0, 0, 0, 0, 0)); /* Invalid instruction */

  const int32_t instruction_opcode = entry->value2;

  switch (map_integer_to_name_value_value(opcode_table, OPCODE_COUNT, instruction_opcode)->value2) {
    case BRANCH_COPROCESSOR_CONDITION_INSTRUCTION:
      return (make_i_instruction(value, instruction_opcode, BINARY_SOURCE_REGISTER(value), BINARY_TARGET_REGISTER(value), value & 0xffff));

    case BRANCH_SOURCE_REGISTER_LABEL_INSTRUCTION:
      return (make_i_instruction(value, instruction_opcode, BINARY_SOURCE_REGISTER(value), 0, value & 0xffff));

    case SOURCE_REGISTER_IMMEDIATE_INSTRUCTION:
      return (make_i_instruction(value, instruction_opcode, BINARY_SOURCE_REGISTER(value), 0, value & 0xffff));

    case TARGET_REGISTER_IMMEDIATE_INSTRUCTION:
      return (make_i_instruction(value, instruction_opcode, BINARY_SOURCE_REGISTER(value), BINARY_TARGET_REGISTER(value), value & 0xffff));

    case TARGET_REGISTER_SOURCE_REGISTER_IMMEDIATE_INSTRUCTION:
    case BRANCH_SOURCE_REGISTER_TARGET_REGISTER_LABEL_INSTRUCTION:
      return (make_i_instruction(value, instruction_opcode, BINARY_SOURCE_REGISTER(value), BINARY_TARGET_REGISTER(value), value & 0xffff));

    case TARGET_REGISTER_ADDRESS_INSTRUCTION:
      return (make_i_instruction(value, instruction_opcode, BINARY_SOURCE_REGISTER(value), BINARY_TARGET_REGISTER(value), value & 0xffff));

    case SOURCE_REGISTER_INSTRUCTION:
      return (make_r_instruction(value, instruction_opcode, BINARY_SOURCE_REGISTER(value), 0, 0, 0));

    case DESTINATION_REGISTER_INSTRUCTION:
      return (make_r_instruction(value, instruction_opcode, 0, 0, BINARY_DESTINATION_REGISTER(value), 0));

    case TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION:
      return (make_r_instruction(value, instruction_opcode, 0, BINARY_TARGET_REGISTER(value), BINARY_DESTINATION_REGISTER(value), 0));

    case SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION:
      return (make_r_instruction(value, instruction_opcode, BINARY_SOURCE_REGISTER(value), BINARY_TARGET_REGISTER(value), 0, 0));

    case DESTINATION_REGISTER_SOURCE_REGISTER_INSTRUCTION:
      return (make_r_instruction(value, instruction_opcode, BINARY_SOURCE_REGISTER(value), 0, BINARY_DESTINATION_REGISTER(value), 0));

    case DESTINATION_REGISTER_TARGET_REGISTER_SHIFT_AMOUNT_INSTRUCTION:
      return (make_r_instruction(value, instruction_opcode, 0, BINARY_TARGET_REGISTER(value), BINARY_DESTINATION_REGISTER(value),
                        BINARY_SHIFT_AMOUNT(value)));

    case DESTINATION_REGISTER_SOURCE_REGISTER_TARGET_REGISTER_INSTRUCTION:
      return (
          make_r_instruction(value, instruction_opcode, BINARY_SOURCE_REGISTER(value), BINARY_TARGET_REGISTER(value), BINARY_DESTINATION_REGISTER(value), 0));

    case DESTINATION_REGISTER_TARGET_REGISTER_SOURCE_REGISTER_INSTRUCTION:
      return (
          make_r_instruction(value, instruction_opcode, BINARY_SOURCE_REGISTER(value), BINARY_TARGET_REGISTER(value), BINARY_DESTINATION_REGISTER(value), 0));

    case FLOATING_POINT_TARGET_ADDRESS_INSTRUCTION:
      return (
          make_i_instruction(value, instruction_opcode, BINARY_BASE(value), BINARY_FLOATING_POINT_TARGET(value), value & 0xffff));

    case FLOATING_POINT_DESTINATION_SOURCE_INSTRUCTION:
      return (make_coprocessor_r_instruction(value, instruction_opcode, BINARY_FLOATING_POINT_SOURCE(value), 0, BINARY_FLOATING_POINT_DESTINATION(value)));

    case FLOATING_POINT_TARGET_REGISTER_COPROCESSOR_REGISTER_INSTRUCTION:
      return (make_r_instruction(value, instruction_opcode, 0, BINARY_TARGET_REGISTER(value), BINARY_FLOATING_POINT_SOURCE(value), 0));

    case FLOATING_POINT_COMPARE_SOURCE_TARGET_INSTRUCTION: {
      /* make_coprocessor_r_instruction, not make_r_instruction: FS lives in the rd slot, so passing FD as
         rd would overwrite it.  The condition goes in the rs slot. */
      mips_instruction* instruction =
          make_coprocessor_r_instruction(value, instruction_opcode, BINARY_FLOATING_POINT_SOURCE(value), BINARY_FLOATING_POINT_TARGET(value), BINARY_FLOATING_POINT_DESTINATION(value));
      SET_CONDITION(instruction, value & 0xf);
      return (instruction);
    }

    case FLOATING_POINT_DESTINATION_SOURCE_TARGET_INSTRUCTION:
      return (
          make_coprocessor_r_instruction(value, instruction_opcode, BINARY_FLOATING_POINT_SOURCE(value), BINARY_FLOATING_POINT_TARGET(value), BINARY_FLOATING_POINT_DESTINATION(value)));

    case MOVE_CONDITIONAL_TYPE_INSTRUCTION:
      return (
          make_r_instruction(value, instruction_opcode, BINARY_SOURCE_REGISTER(value), BINARY_TARGET_REGISTER(value), BINARY_DESTINATION_REGISTER(value), 0));

    case FLOATING_POINT_MOVE_CONDITIONAL_TYPE_INSTRUCTION:
      /* FS in the rd slot, the GPR (movn/movz) or cc<<2|tf (movf/movt) in rt,
         FD in shamt -- the layout emit_floating_point_r builds. */
      return (
          make_coprocessor_r_instruction(value, instruction_opcode, BINARY_FLOATING_POINT_SOURCE(value), BINARY_TARGET_REGISTER(value), BINARY_FLOATING_POINT_DESTINATION(value)));

    case J_TYPE_INSTRUCTION:
      return (make_j_instruction(value, instruction_opcode, value & 0x3ffffff));

    case NO_ARGUMENT_TYPE_INSTRUCTION:
      return (make_r_instruction(value, instruction_opcode, 0, 0, 0, 0));

    default:
      return (make_r_instruction(value, 0, 0, 0, 0, 0)); /* Invalid instruction */
  }
}

/* The decode constructors below keep zmalloc + the SET_* accessors rather
   than a designated initializer: the accessors are the one place that maps
   MIPS field names onto mips_instruction's overlapping unions (FS lives in
   the rd slot, FD in shamt, IMMEDIATE_OFFSET in imm), and an initializer would
   restate that mapping at every site.  zmalloc supplies the zero fill. */

static mips_instruction* make_r_instruction(int32_t value, int opcode, int source_register, int target_register,
                                   int destination_register, int shift_amount) {
  mips_instruction* instruction =
      (mips_instruction*)allocate_zeroed_or_exit(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_SOURCE_REGISTER(instruction, source_register);
  SET_TARGET_REGISTER(instruction, target_register);
  SET_DESTINATION_REGISTER(instruction, destination_register);
  SET_SHIFT_AMOUNT(instruction, shift_amount);
  SET_ENCODING(instruction, value);
  SET_EXPRESSION(instruction, nullptr);
  return (instruction);
}

static mips_instruction* make_coprocessor_r_instruction(int32_t value, int opcode, int floating_point_source, int floating_point_target,
                                      int floating_point_destination) {
  mips_instruction* instruction =
      (mips_instruction*)allocate_zeroed_or_exit(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_FLOATING_POINT_SOURCE(instruction, floating_point_source);
  SET_FLOATING_POINT_TARGET(instruction, floating_point_target);
  SET_FLOATING_POINT_DESTINATION(instruction, floating_point_destination);
  SET_ENCODING(instruction, value);
  SET_EXPRESSION(instruction, nullptr);
  return (instruction);
}

static mips_instruction* make_i_instruction(int32_t value, int opcode, int source_register, int target_register,
                                   int offset) {
  mips_instruction* instruction =
      (mips_instruction*)allocate_zeroed_or_exit(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_SOURCE_REGISTER(instruction, source_register);
  SET_TARGET_REGISTER(instruction, target_register);
  SET_IMMEDIATE_OFFSET(instruction, offset);
  SET_ENCODING(instruction, value);
  SET_EXPRESSION(instruction, nullptr);
  return (instruction);
}

static mips_instruction* make_j_instruction(int32_t value, int opcode, int target) {
  mips_instruction* instruction =
      (mips_instruction*)allocate_zeroed_or_exit(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_TARGET(instruction, target);
  SET_ENCODING(instruction, value);
  SET_EXPRESSION(instruction, nullptr);
  return (instruction);
}

/* Code to test encode/decode of instructions. */

void test_assembly(mips_instruction* instruction) {
  mips_instruction* const new_instruction = instruction_decode(instruction_encode(instruction));

  compare_instructions(instruction, new_instruction);
  free_instruction(new_instruction);
}

static void compare_instructions(mips_instruction* instruction1, mips_instruction* instruction2) {
  static string_stream stream;

  stream_clear(&stream);
  if (instruction1->opcode != instruction2->opcode || instruction1->encoding != instruction2->encoding ||
      SOURCE_REGISTER(instruction1) != SOURCE_REGISTER(instruction2) || TARGET_REGISTER(instruction1) != TARGET_REGISTER(instruction2) ||
      DESTINATION_REGISTER(instruction1) != DESTINATION_REGISTER(instruction2) || SHIFT_AMOUNT(instruction1) != SHIFT_AMOUNT(instruction2)) {
    stream_printf(&stream, "=================== Not Equal ===================\n");
    format_an_instruction(&stream, instruction1, 0);
    stream_printf(&stream, "===================\n");
    format_an_instruction(&stream, instruction2, 0);
    stream_printf(&stream, "=================== Not Equal ===================\n");
    error("%s", stream_to_string(&stream));
  }
}
