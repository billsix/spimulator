
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

static imm_expr* copy_imm_expr(const imm_expr* old_expression);
static void increment_text_pc(int delta);
static imm_expr* lower_bits_of_expr(const imm_expr* old_expression);
static void store_instruction(mips_instruction* instruction);
static imm_expr* upper_bits_of_expr(const imm_expr* old_expression);

static void format_imm_expr(str_stream* stream, imm_expr* expression, int base_register);
static void i_type_full_word(int opcode, int target_register, int source_register, const imm_expr* expression,
                             bool value_known, int32_t value);
static void inst_cmp(mips_instruction* instruction1, mips_instruction* instruction2);
static mips_instruction* make_r_type(int opcode, int destination_register, int source_register, int target_register);
static mips_instruction* mk_i_inst(int32_t value, int opcode, int source_register, int target_register,
                                   int offset);
static mips_instruction* mk_j_inst(int32_t value, int opcode, int target);
static mips_instruction* mk_r_inst(int32_t value, int opcode, int source_register, int target_register,
                                   int destination_register, int shift_amount);
static mips_instruction* mk_co_r_inst(int32_t value, int opcode, int floating_point_destination, int floating_point_source,
                                      int floating_point_target);
static void produce_immediate(const imm_expr* expression, int target_register, bool value_known,
                              int32_t value);

/* Local variables: */

/* True means store instructions in kernel, not user, text segment */

static bool in_kernel = false;

/* Instruction used as breakpoint by SPIM: */

static mips_instruction* break_inst = nullptr;

/* Locations for next instruction in user and kernel text segments */

static mem_addr next_text_pc;

static mem_addr next_k_text_pc;

#define INST_PC (in_kernel ? next_k_text_pc : next_text_pc)

/* Set ADDRESS at which the next instruction is stored. */

void text_begins_at_point(mem_addr address) {
  next_text_pc = address;
  in_kernel = false;
}

void k_text_begins_at_point(mem_addr address) {
  next_k_text_pc = address;
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
    next_k_text_pc =
        (next_k_text_pc + (1u << alignment) - 1) & (0xffffffff << alignment);
    fix_current_label_address(next_k_text_pc);
  } else {
    next_text_pc =
        (next_text_pc + (1u << alignment) - 1) & (0xffffffff << alignment);
    fix_current_label_address(next_text_pc);
  }
}

/* Set the location (in user or kernel text space) for the next instruction. */

void set_text_pc(mem_addr address) {
  if (in_kernel)
    next_k_text_pc = address;
  else
    next_text_pc = address;
}

/* Return address for next instruction, in appropriate text segment. */

mem_addr current_text_pc(void) { return (INST_PC); }

/* Increment the current text segement PC. */

static void increment_text_pc(int delta) {
  if (in_kernel) {
    next_k_text_pc += (mem_addr)delta;
    if (k_text_top <= next_k_text_pc)
      run_error("Can't expand kernel text segment\n");
  } else {
    next_text_pc += (mem_addr)delta;
    if (text_top <= next_text_pc) run_error("Can't expand text segment\n");
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
static mips_instruction* expansion_insts[EXPANSION_MAX];
static int expansion_len = 0;

void expansion_begin(const char* mnemonic, bool is_pseudo) {
  if (expansion_depth++ > 0) return;
  /* The mnemonic's own string may be freed with its AST; keep the
     opcode table's copy. */
  const name_val_val* const entry =
      map_string_to_name_val_val(opcode_tbl, OPCODE_COUNT, mnemonic);
  expansion_mnemonic = entry != nullptr ? entry->name : nullptr;
  expansion_is_pseudo = is_pseudo;
  expansion_len = 0;
}

void expansion_end(void) {
  if (--expansion_depth > 0) return;
  if (expansion_mnemonic != nullptr &&
      (expansion_is_pseudo || expansion_len > 1)) {
    for (int i = 0; i < expansion_len; i++) {
      expansion_insts[i]->expansion_of = expansion_mnemonic;
      expansion_insts[i]->expansion_index = (uint8_t)(i + 1);
      expansion_insts[i]->expansion_count = (uint8_t)expansion_len;
      expansion_insts[i]->expansion_of_pseudo = expansion_is_pseudo;
    }
  } else {
    /* Written as itself: the instruction keeps no expansion record. */
  }
  expansion_len = 0;
}

/* Store an INSTRUCTION in memory at the next location. */

static void store_instruction(mips_instruction* instruction) {
  if (data_dir) {
    store_word(inst_encode(instruction));
    free_inst(instruction);
  } else if (text_dir) {
    const mem_addr at = INST_PC;
    exception_occurred = false;
    mem_write_inst(at, instruction);
    if (exception_occurred)
      error("Invalid address (0x%08x) for instruction\n", at);
    else
      increment_text_pc(BYTES_PER_WORD);
    if (instruction != nullptr) {
      SET_SOURCE(instruction,
                 emit_source_active
                     ? (emit_source_text ? strdup(emit_source_text) : nullptr)
                     : source_line());
      if (ENCODING(instruction) == 0)
        SET_ENCODING(instruction, inst_encode(instruction));
      if (expansion_depth > 0 && expansion_len < EXPANSION_MAX)
        expansion_insts[expansion_len++] = instruction;
      asm_fire_text_inst(at, (uint32_t)ENCODING(instruction));
    }
  } else {
    /* Never reached: the parser always sets data_directive and text_directive as a pair,
       one of them true. */
    fatal_error(
        "internal error: store_instruction: neither the data nor the text "
        "segment is active\n");
  }
}

void i_type_free(int opcode, int target_register, int source_register, imm_expr* expression) {
  i_type(opcode, target_register, source_register, expression);
  free(expression);
}

/* Produce an immediate instruction with the OPCODE, RT, RS, and IMM
   fields.  NB, because the immediate value may not fit in the field,
   this routine may produce more than one instruction.	On the bare
   machine, we resolve symbolic address, but they better produce values
   that fit into instruction's immediate field. */

void i_type(int opcode, int target_register, int source_register, const imm_expr* expression) {
  mips_instruction* instruction =
      (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_RS(instruction, source_register);
  SET_RT(instruction, target_register);
  SET_EXPR(instruction, copy_imm_expr(expression));
  if (expression->symbol == nullptr || SYMBOL_IS_DEFINED(expression->symbol)) {
    /* Evaluate the instruction's expression. */
    const int32_t value = eval_imm_expr(expression);

    if (!bare_machine &&
        (((opcode == TOK_ADDI_OPCODE || opcode == TOK_ADDIU_OPCODE ||
           opcode == TOK_SLTI_OPCODE || opcode == TOK_SLTIU_OPCODE ||
           opcode == TOK_TEQI_OPCODE || opcode == TOK_TGEI_OPCODE ||
           opcode == TOK_TGEIU_OPCODE || opcode == TOK_TLTI_OPCODE ||
           opcode == TOK_TLTIU_OPCODE || opcode == TOK_TNEI_OPCODE ||
           (opcode_is_load_store(opcode) && expression->bits == 0))
              // Sign-extended immediate values:
              ? (((unsigned int)value & 0xffff8000) != 0 &&
                 ((unsigned int)value & 0xffff8000) != 0xffff8000)
              // Not sign-extended:
              : ((unsigned int)value & 0xffff0000) != 0))) {
      // Non-immediate value
      free_inst(instruction);
      i_type_full_word(opcode, target_register, source_register, expression, true, value);
      return;
    } else
      resolve_a_label(expression->symbol, instruction);
  } else if (bare_machine || expression->bits != 0)
    /* Don't know expression's value, but only needed upper/lower 16-bits
       anyways. */
    record_inst_uses_symbol(instruction, expression->symbol);
  else {
    /* Don't know the expressions's value and want all of its bits,
       so assume that it will not produce a small result and generate
       sequence for 32 bit value. */
    free_inst(instruction);

    i_type_full_word(opcode, target_register, source_register, expression, false, 0);
    return;
  }

  store_instruction(instruction);
}

/* The immediate value for an instruction will (or may) not fit in 16 bits.
   Build the value from its piece with separate instructions. */

static void i_type_full_word(int opcode, int target_register, int source_register, const imm_expr* expression,
                             bool value_known, int32_t value) {
  if (opcode_is_load_store(opcode)) {
    int32_t offset;

    if (expression->symbol != nullptr && expression->symbol->global_pointer_flag && source_register == 0 &&
        IMM_MIN <= (offset = (int32_t)(expression->symbol->address + expression->offset)) &&
        offset <= IMM_MAX) {
      i_type_free(opcode, target_register, REG_GP, make_imm_expr(offset, nullptr, false));
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

        i_type_free(TOK_LUI_OPCODE, 1, 0, const_imm_expr(high));
        if (source_register != 0) /* Base register */
        {
          r_type(TOK_ADDU_OPCODE, 1, 1, source_register);
        }
        /* lower_bits_of_expr copies its argument, so the const_imm_expr
           node it wraps is not adopted by anyone — free the intermediate. */
        imm_expr* const low_expression = const_imm_expr(low);
        i_type_free(opcode, target_register, 1, lower_bits_of_expr(low_expression));
        free(low_expression);
      } else {
        /* Special case, sign-extension of low 16 bits sets high to 0xffff */
        i_type_free(opcode, target_register, source_register, const_imm_expr(low));
      }
    } else {
      /* Use $at */
      /* Need to adjust if lower bits are negative */
      i_type_free(TOK_LUI_OPCODE, 1, 0, upper_bits_of_expr(expression));
      if (source_register != 0) /* Base register */
      {
        r_type(TOK_ADDU_OPCODE, 1, 1, source_register);
      }
      i_type_free(opcode, target_register, 1, lower_bits_of_expr(expression));
    }
  } else if (opcode_is_branch(opcode)) {
    /* This only allows branches +/- 32K, which is not correct! */
    i_type_free(opcode, target_register, source_register, lower_bits_of_expr(expression));
  } else
  /* Computation instruction */
  {
    int offset;

    if (expression->symbol != nullptr && expression->symbol->global_pointer_flag && source_register == 0 &&
        IMM_MIN <= (offset = (int32_t)(expression->symbol->address + expression->offset)) &&
        offset <= IMM_MAX) {
      i_type_free((opcode == TOK_LUI_OPCODE ? TOK_ADDIU_OPCODE : opcode), target_register,
                  REG_GP, make_imm_expr(offset, nullptr, false));
    } else {
      /* Use $at */
      if ((opcode == TOK_ORI_OPCODE || opcode == TOK_ADDI_OPCODE ||
           opcode == TOK_ADDIU_OPCODE || opcode == TOK_LUI_OPCODE) &&
          source_register == 0) {
        produce_immediate(expression, target_register, value_known, value);
      } else {
        produce_immediate(expression, 1, value_known, value);
        r_type(imm_op_to_op(opcode), target_register, source_register, 1);
      }
    }
  }
}

static void produce_immediate(const imm_expr* expression, int target_register, bool value_known,
                              int32_t value) {
  if (value_known && (value & 0xffff) == 0) {
    i_type_free(TOK_LUI_OPCODE, target_register, 0, upper_bits_of_expr(expression));
  } else if (value_known && ((unsigned int)value & 0xffff0000) == 0) {
    i_type_free(TOK_ORI_OPCODE, target_register, 0, lower_bits_of_expr(expression));
  } else {
    i_type_free(TOK_LUI_OPCODE, 1, 0, upper_bits_of_expr(expression));
    i_type_free(TOK_ORI_OPCODE, target_register, 1, lower_bits_of_expr(expression));
  }
}

/* Return a jump-type instruction with the given OPCODE and TARGET
   fields. NB, even the immediate value may not fit in the field, this
   routine will not produce more than one instruction. */

void j_type(int opcode, imm_expr* target) {
  mips_instruction* instruction =
      (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  target->offset = 0; /* Not PC relative */
  target->program_counter_relative = false;
  SET_EXPR(instruction, copy_imm_expr(target));
  if (target->symbol == nullptr || SYMBOL_IS_DEFINED(target->symbol))
    resolve_a_label(target->symbol, instruction);
  else
    record_inst_uses_symbol(instruction, target->symbol);
  store_instruction(instruction);
}

/* Return a register-type instruction with the given OPCODE, RD, RS, and RT
   fields. */

static mips_instruction* make_r_type(int opcode, int destination_register, int source_register, int target_register) {
  mips_instruction* instruction =
      (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_RS(instruction, source_register);
  SET_RT(instruction, target_register);
  SET_RD(instruction, destination_register);
  return (instruction);
}

/* Return a register-type instruction with the given OPCODE, RD, RS, and RT
   fields. */

void r_type(int opcode, int destination_register, int source_register, int target_register) {
  store_instruction(make_r_type(opcode, destination_register, source_register, target_register));
}

/* Return a register-type instruction with the given OPCODE, FD, FS, and FT
   fields. */

void r_co_type(int opcode, int floating_point_destination, int floating_point_source, int floating_point_target) {
  mips_instruction* instruction = make_r_type(opcode, floating_point_source, 0, floating_point_target);
  SET_FD(instruction, floating_point_destination);
  store_instruction(instruction);
}

/* Return a register-shift instruction with the given OPCODE, RD, RT, and
   SHAMT fields.*/

void r_sh_type(int opcode, int destination_register, int target_register, int shift_amount) {
  mips_instruction* instruction = make_r_type(opcode, destination_register, 0, target_register);
  SET_SHAMT(instruction, shift_amount & 0x1f);
  store_instruction(instruction);
}

/* Return a floating-point compare instruction with the given OPCODE,
   FS, FT, and CC fields.*/

void r_cond_type(int opcode, int floating_point_source, int floating_point_target, int condition_code) {
  mips_instruction* instruction = make_r_type(opcode, floating_point_source, 0, floating_point_target);
  SET_FD(instruction, condition_code << 2);
  switch (opcode) {
    case TOK_C_EQ_D_OPCODE:
    case TOK_C_EQ_S_OPCODE: {
      SET_COND(instruction, COND_EQ);
      break;
    }

    case TOK_C_LE_D_OPCODE:
    case TOK_C_LE_S_OPCODE: {
      SET_COND(instruction, COND_IN | COND_LT | COND_EQ);
      break;
    }

    case TOK_C_LT_D_OPCODE:
    case TOK_C_LT_S_OPCODE: {
      SET_COND(instruction, COND_IN | COND_LT);
      break;
    }

    case TOK_C_NGE_D_OPCODE:
    case TOK_C_NGE_S_OPCODE: {
      SET_COND(instruction, COND_IN | COND_LT | COND_UN);
      break;
    }

    case TOK_C_NGLE_D_OPCODE:
    case TOK_C_NGLE_S_OPCODE: {
      SET_COND(instruction, COND_IN | COND_UN);
      break;
    }

    case TOK_C_NGL_D_OPCODE:
    case TOK_C_NGL_S_OPCODE: {
      SET_COND(instruction, COND_IN | COND_EQ | COND_UN);
      break;
    }

    case TOK_C_NGT_D_OPCODE:
    case TOK_C_NGT_S_OPCODE: {
      SET_COND(instruction, COND_IN | COND_LT | COND_EQ | COND_UN);
      break;
    }

    case TOK_C_OLT_D_OPCODE:
    case TOK_C_OLT_S_OPCODE: {
      SET_COND(instruction, COND_LT);
      break;
    }

    case TOK_C_OLE_D_OPCODE:
    case TOK_C_OLE_S_OPCODE: {
      SET_COND(instruction, COND_LT | COND_EQ);
      break;
    }

    case TOK_C_SEQ_D_OPCODE:
    case TOK_C_SEQ_S_OPCODE: {
      SET_COND(instruction, COND_IN | COND_EQ);
      break;
    }

    case TOK_C_SF_D_OPCODE:
    case TOK_C_SF_S_OPCODE: {
      SET_COND(instruction, COND_IN);
      break;
    }

    case TOK_C_F_D_OPCODE:
    case TOK_C_F_S_OPCODE: {
      SET_COND(instruction, 0);
      break;
    }

    case TOK_C_UEQ_D_OPCODE:
    case TOK_C_UEQ_S_OPCODE: {
      SET_COND(instruction, COND_EQ | COND_UN);
      break;
    }

    case TOK_C_ULT_D_OPCODE:
    case TOK_C_ULT_S_OPCODE: {
      SET_COND(instruction, COND_LT | COND_UN);
      break;
    }

    case TOK_C_ULE_D_OPCODE:
    case TOK_C_ULE_S_OPCODE: {
      SET_COND(instruction, COND_LT | COND_EQ | COND_UN);
      break;
    }

    case TOK_C_UN_D_OPCODE:
    case TOK_C_UN_S_OPCODE: {
      SET_COND(instruction, COND_UN);
      break;
    }
    default:
      /* Never reached: only FP_CMP_FS_FT_INST rows reach here, and the cases
         above are all 32 of them (16 conditions, .s and .d). */
      fatal_error("internal error: r_cond_type: not an FP compare: %d\n",
                  opcode);
  }
  store_instruction(instruction);
}

/* Make and return a deep copy of INST. */

mips_instruction* copy_inst(const mips_instruction* instruction) {
  mips_instruction* new_instruction =
      (mips_instruction*)xmalloc(sizeof(mips_instruction));

  *new_instruction = *instruction;
  /*memcpy ((void*)new_instruction, (void*)instruction , sizeof (instruction));*/
  SET_EXPR(new_instruction, copy_imm_expr(EXPR(instruction)));
  return (new_instruction);
}

void free_inst(mips_instruction* instruction) {
  if (instruction != break_inst)
  /* Don't free the breakpoint insructions since we only have one. */
  {
    if (EXPR(instruction)) free(EXPR(instruction));
    free(instruction);
  }
}

/* The opcode tables (opcode_table, opcode_encoding_table, opcode_decode_table) are
   generated from include/opcodes.tsv into src/opcode-tables.c, already in
   the order map_int_to_name_val_val's bsearch needs; see opcode-tables.h. */

/* Print the instruction stored at the memory ADDRESS. */

void print_inst(mem_addr address) {
  char* const instruction_string = inst_to_string(address);
  write_output(message_out, "%s", instruction_string);
  free(instruction_string);
}

/* Return the canonical mnemonic for instruction (e.g. "addu", "ori", "j"), or
   "<unknown>" if OPCODE(instruction) isn't in opcode_table. The returned pointer
   is owned by the static opcode_table and must not be freed. */
const char* inst_op_name(const mips_instruction* instruction) {
  if (instruction == nullptr) return "<null>";
  const name_val_val* const entry =
      map_int_to_name_val_val(opcode_tbl, OPCODE_COUNT, OPCODE(instruction));
  return entry ? entry->name : "<unknown>";
}

/* Op-token variant: take a raw opcode token (TOK_*_OP, TOK_*_POP) and
   return its mnemonic — used by the AST builder to label AST_PSEUDO
   nodes. */
const char* op_token_name(int opcode_token) {
  const name_val_val* const entry =
      map_int_to_name_val_val(opcode_tbl, OPCODE_COUNT, opcode_token);
  return entry ? entry->name : "<unknown>";
}

char* inst_to_string(mem_addr address) {
  /* A side-effect-free read: showing an instruction must not raise, or
     clear, the exception the program's own step raised. */
  mips_instruction* instruction = nullptr;
  if (!mem_peek_inst(address, &instruction)) {
    error("Can't print instruction not in text segment (0x%08x)\n", address);
    /* Callers free the result, so it must be heap-allocated — a string
       literal here would get free()d. */
    return strdup("");
  }

  str_stream stream;
  ss_init(&stream);
  format_an_inst(&stream, instruction, address);
  /* Transfer the buffer to the caller (who frees it); ss is a local,
     so its stream must not outlive this call. */
  return ss_take_string(&stream);
}

void format_an_inst(str_stream* stream, mips_instruction* instruction,
                    mem_addr address) {
  const name_val_val* entry;
  const int line_start = ss_length(stream);

  if (address != 0 && inst_is_breakpoint(address)) {
    delete_breakpoint(address);
    ss_printf(stream, "*");
    format_an_inst(stream, mem_read_inst(address), address);
    add_breakpoint(address);
    return;
  }

  ss_printf(stream, "[0x%08x]\t", address);
  if (instruction == nullptr) {
    ss_printf(stream, "<none>\n");
    return;
  }

  entry =
      map_int_to_name_val_val(opcode_tbl, OPCODE_COUNT, OPCODE(instruction));
  if (entry == nullptr) {
    ss_printf(stream, "<unknown instruction %d>\n", OPCODE(instruction));
    return;
  }

  ss_printf(stream, "0x%08x  %s", (uint32_t)ENCODING(instruction), entry->name);
  switch (entry->value2) {
    case BRANCH_COPROC_COND_INST:
      ss_printf(stream, "%d %d", CC(instruction), (int)BRANCH_OFFSET(instruction));
      break;

    case BRANCH_RS_LABEL_INST:
      ss_printf(stream, " $%s %d", int_reg_names[RS(instruction)],
                (int)BRANCH_OFFSET(instruction));
      break;

    case RS_IMM_INST:
      ss_printf(stream, " $%s, %d", int_reg_names[RS(instruction)],
                IMM(instruction));
      break;

    case RT_IMM_INST:
      ss_printf(stream, " $%s, %d", int_reg_names[RT(instruction)],
                IMM(instruction));
      break;

    case RT_RS_IMM_INST:
      ss_printf(stream, " $%s, $%s, %d", int_reg_names[RT(instruction)],
                int_reg_names[RS(instruction)], IMM(instruction));
      break;

    case BRANCH_RS_RT_LABEL_INST:
      ss_printf(stream, " $%s, $%s, %d", int_reg_names[RS(instruction)],
                int_reg_names[RT(instruction)],
                (int)BRANCH_OFFSET(instruction));
      break;

    case RT_ADDR_INST:
      ss_printf(stream, " $%s, %d($%s)", int_reg_names[RT(instruction)],
                IMM(instruction), int_reg_names[BASE(instruction)]);
      break;

    case RS_INST:
      ss_printf(stream, " $%s", int_reg_names[RS(instruction)]);
      break;

    case RD_INST:
      ss_printf(stream, " $%s", int_reg_names[RD(instruction)]);
      break;

    case RT_COPREG_INST:
      ss_printf(stream, " $%s, $%s", int_reg_names[RT(instruction)],
                int_reg_names[RD(instruction)]);
      break;

    case RS_RT_INST:
      ss_printf(stream, " $%s, $%s", int_reg_names[RS(instruction)],
                int_reg_names[RT(instruction)]);
      break;

    case RD_RS_INST:
      ss_printf(stream, " $%s, $%s", int_reg_names[RD(instruction)],
                int_reg_names[RS(instruction)]);
      break;

    case RD_RT_SHAMT_INST:
      /* nop is canonically sll $0, $0, 0. ENCODING() is unreliable here
       * (the assembler doesn't populate it for source-assembled instructions,
       * so it's always 0 and would falsely make every shift print as nop).
       * Check the actual fields instead. */
      if (RD(instruction) == 0 && RT(instruction) == 0 &&
          SHAMT(instruction) == 0) {
        ss_erase(stream, 3); /* zap sll */
        ss_printf(stream, "nop");
      } else
        ss_printf(stream, " $%s, $%s, %d", int_reg_names[RD(instruction)],
                  int_reg_names[RT(instruction)], SHAMT(instruction));
      break;

    case RD_RS_RT_INST:
      ss_printf(stream, " $%s, $%s, $%s", int_reg_names[RD(instruction)],
                int_reg_names[RS(instruction)], int_reg_names[RT(instruction)]);
      break;

    case RD_RT_RS_INST:
      ss_printf(stream, " $%s, $%s, $%s", int_reg_names[RD(instruction)],
                int_reg_names[RT(instruction)], int_reg_names[RS(instruction)]);
      break;

    case FP_FT_ADDR_INST:
      ss_printf(stream, " $f%d, %d($%s)", FT(instruction), IMM(instruction),
                int_reg_names[BASE(instruction)]);
      break;

    case FP_FD_FS_INST:
      ss_printf(stream, " $f%d, $f%d", FD(instruction), FS(instruction));
      break;

    case FP_RT_COPREG_INST:
      ss_printf(stream, " $%s, $f%d", int_reg_names[RT(instruction)],
                FS(instruction));
      break;

    case FP_CMP_FS_FT_INST:
      if (FD(instruction) == 0)
        ss_printf(stream, " $f%d, $f%d", FS(instruction), FT(instruction));
      else
        ss_printf(stream, " %d, $f%d, $f%d", FD(instruction) >> 2, FS(instruction),
                  FT(instruction));
      break;

    case FP_FD_FS_FT_INST:
      ss_printf(stream, " $f%d, $f%d, $f%d", FD(instruction), FS(instruction),
                FT(instruction));
      break;

    case MOVC_TYPE_INST:
      ss_printf(stream, " $%s, $%s, %d", int_reg_names[RD(instruction)],
                int_reg_names[RS(instruction)], RT(instruction) >> 2);
      break;

    case FP_MOVC_TYPE_INST:
      switch (OPCODE(instruction)) {
        case TOK_MOVN_S_OPCODE:
        case TOK_MOVN_D_OPCODE:
        case TOK_MOVZ_S_OPCODE:
        case TOK_MOVZ_D_OPCODE:
          /* movn/movz test a general register */
          ss_printf(stream, " $f%d, $f%d, $%s", FD(instruction), FS(instruction),
                    int_reg_names[RT(instruction)]);
          break;
        default:
          /* movf/movt test a condition code */
          ss_printf(stream, " $f%d, $f%d, %d", FD(instruction), FS(instruction),
                    CC(instruction));
          break;
      }
      break;

    case J_TYPE_INST:
      ss_printf(stream, " 0x%08x", TARGET(instruction) << 2);
      break;

    case NOARG_TYPE_INST:
      break;

    default:
      fatal_error("Unknown instruction type in print_inst\n");
  }

  if (EXPR(instruction) != nullptr && EXPR(instruction)->symbol != nullptr) {
    ss_printf(stream, " [");
    if (opcode_is_load_store(OPCODE(instruction)))
      format_imm_expr(stream, EXPR(instruction), BASE(instruction));
    else
      format_imm_expr(stream, EXPR(instruction), -1);
    ss_printf(stream, "]");
  }

  if (SOURCE(instruction) != nullptr) {
    /* Comment is source line text of current line. */
    int gap_length = 57 - (ss_length(stream) - line_start);
    for (; 0 < gap_length; gap_length -= 1) {
      ss_printf(stream, " ");
    }

    ss_printf(stream, "; ");
    ss_printf(stream, "%s", SOURCE(instruction));
  }

  ss_printf(stream, "\n");
}

/* Return true if SPIM OPCODE (e.g. Y_...) represents a conditional
   branch. */

bool opcode_is_branch(int opcode) {
  switch (opcode) {
    case TOK_BC1F_OPCODE:
    case TOK_BC1FL_OPCODE:
    case TOK_BC1T_OPCODE:
    case TOK_BC1TL_OPCODE:
    case TOK_BC2F_OPCODE:
    case TOK_BC2FL_OPCODE:
    case TOK_BC2T_OPCODE:
    case TOK_BC2TL_OPCODE:
    case TOK_BEQ_OPCODE:
    case TOK_BEQL_OPCODE:
    case TOK_BEQZ_PSEUDO_OP:
    case TOK_BGE_PSEUDO_OP:
    case TOK_BGEU_PSEUDO_OP:
    case TOK_BGEZ_OPCODE:
    case TOK_BGEZAL_OPCODE:
    case TOK_BGEZALL_OPCODE:
    case TOK_BGEZL_OPCODE:
    case TOK_BGT_PSEUDO_OP:
    case TOK_BGTU_PSEUDO_OP:
    case TOK_BGTZ_OPCODE:
    case TOK_BGTZL_OPCODE:
    case TOK_BLE_PSEUDO_OP:
    case TOK_BLEU_PSEUDO_OP:
    case TOK_BLEZ_OPCODE:
    case TOK_BLEZL_OPCODE:
    case TOK_BLT_PSEUDO_OP:
    case TOK_BLTU_PSEUDO_OP:
    case TOK_BLTZ_OPCODE:
    case TOK_BLTZAL_OPCODE:
    case TOK_BLTZALL_OPCODE:
    case TOK_BLTZL_OPCODE:
    case TOK_BNE_OPCODE:
    case TOK_BNEL_OPCODE:
    case TOK_BNEZ_PSEUDO_OP:
      return true;

    default:
      return false;
  }
}

/* Return true if SPIM OPCODE represents a nullified (e.g., Y_...L_OP)
   conditional branch. */

bool opcode_is_nullified_branch(int opcode) {
  switch (opcode) {
    case TOK_BC1FL_OPCODE:
    case TOK_BC1TL_OPCODE:
    case TOK_BC2FL_OPCODE:
    case TOK_BC2TL_OPCODE:
    case TOK_BEQL_OPCODE:
    case TOK_BGEZALL_OPCODE:
    case TOK_BGEZL_OPCODE:
    case TOK_BGTZL_OPCODE:
    case TOK_BLEZL_OPCODE:
    case TOK_BLTZALL_OPCODE:
    case TOK_BLTZL_OPCODE:
    case TOK_BNEL_OPCODE:
      return true;

    default:
      return false;
  }
}

/* Return true if SPIM OPCODE (e.g. Y_...) represents a conditional
   branch on a true condition. */

bool opcode_is_true_branch(int opcode) {
  switch (opcode) {
    case TOK_BC1T_OPCODE:
    case TOK_BC1TL_OPCODE:
    case TOK_BC2T_OPCODE:
    case TOK_BC2TL_OPCODE:
      return true;

    default:
      return false;
  }
}

/* Return true if SPIM OPCODE (e.g. Y_...) is a direct unconditional
   branch (jump). */

bool opcode_is_jump(int opcode) {
  switch (opcode) {
    case TOK_J_OPCODE:
    case TOK_JAL_OPCODE:
      return true;

    default:
      return false;
  }
}

/* Return true if SPIM OPCODE (e.g. Y_...) is a load or store. */

bool opcode_is_load_store(int opcode) {
  switch (opcode) {
    case TOK_LB_OPCODE:
    case TOK_LBU_OPCODE:
    case TOK_LH_OPCODE:
    case TOK_LHU_OPCODE:
    case TOK_LL_OPCODE:
    case TOK_LDC1_OPCODE:
    case TOK_LDC2_OPCODE:
    case TOK_LW_OPCODE:
    case TOK_LWC1_OPCODE:
    case TOK_LWC2_OPCODE:
    case TOK_LWL_OPCODE:
    case TOK_LWR_OPCODE:
    case TOK_SB_OPCODE:
    case TOK_SC_OPCODE:
    case TOK_SH_OPCODE:
    case TOK_SDC1_OPCODE:
    case TOK_SDC2_OPCODE:
    case TOK_SW_OPCODE:
    case TOK_SWC1_OPCODE:
    case TOK_SWC2_OPCODE:
    case TOK_SWL_OPCODE:
    case TOK_SWR_OPCODE:
      return true;

    default:
      return false;
  }
}

/* Return true if a breakpoint is set at ADDR. */

bool inst_is_breakpoint(mem_addr address) {
  if (break_inst == nullptr)
    break_inst = make_r_type(TOK_BREAK_OPCODE, 1, 0, 0);

  return (mem_read_inst(address) == break_inst);
}

/* Set a breakpoint at ADDR and return the old instruction.  If the
   breakpoint cannot be set, return nullptr. */

mips_instruction* set_breakpoint(mem_addr address) {
  if (break_inst == nullptr)
    break_inst = make_r_type(TOK_BREAK_OPCODE, 1, 0, 0);

  exception_occurred = false;
  mips_instruction* const old_instruction = mem_read_inst(address);
  if (old_instruction == break_inst) return (nullptr);

  mem_write_inst(address, break_inst);
  if (exception_occurred)
    return (nullptr);
  else
    return (old_instruction);
}

/* An immediate expression has the form: SYMBOL +/- IOFFSET, where either
   part may be omitted. */

/* Make and return a new immediate expression */

imm_expr* make_imm_expr(int offset, const char* symbol, bool is_program_counter_relative) {
  imm_expr* expression = (imm_expr*)xmalloc(sizeof(imm_expr));

  *expression = (imm_expr){
      .offset = offset,
      .symbol = symbol != nullptr ? lookup_label(symbol) : nullptr,
      .program_counter_relative = is_program_counter_relative,
  };
  return (expression);
}

/* Return a shallow copy of the EXPRESSION. */

static imm_expr* copy_imm_expr(const imm_expr* old_expression) {
  imm_expr* expression = (imm_expr*)xmalloc(sizeof(imm_expr));

  *expression = *old_expression;
  /*memcpy ((void*)expr, (void*)old_expression, sizeof (imm_expr));*/
  return (expression);
}

/* Return a shallow copy of an EXPRESSION that only uses the upper
   sixteen bits of the expression's value. */

static imm_expr* upper_bits_of_expr(const imm_expr* old_expression) {
  imm_expr* expression = copy_imm_expr(old_expression);

  expression->bits = 1;
  return (expression);
}

/* Return a shallow copy of the EXPRESSION that only uses the lower
   sixteen bits of the expression's value. */

static imm_expr* lower_bits_of_expr(const imm_expr* old_expression) {
  imm_expr* expression = copy_imm_expr(old_expression);

  expression->bits = -1;
  return (expression);
}

/* Return an instruction expression for a constant VALUE. */

imm_expr* const_imm_expr(int32_t value) {
  return (make_imm_expr(value, nullptr, false));
}

/* Return a shallow copy of the EXPRESSION with the offset field
   incremented by the given amount. */

imm_expr* incr_expr_offset(const imm_expr* expression, int32_t value) {
  imm_expr* new_expression = copy_imm_expr(expression);

  new_expression->offset += value;
  return (new_expression);
}

/* Return the value of the EXPRESSION. */

int32_t eval_imm_expr(const imm_expr* expression) {
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

static void format_imm_expr(str_stream* stream, imm_expr* expression, int base_register) {
  if (expression->symbol != nullptr) {
    ss_printf(stream, "%s", expression->symbol->name);
  }

  if (expression->program_counter_relative)
    ss_printf(stream, "-0x%08x", (unsigned int)-expression->offset);
  else if (expression->offset < -10)
    ss_printf(stream, "-%d (-0x%08x)", -expression->offset, (unsigned int)-expression->offset);
  else if (expression->offset > 10)
    ss_printf(stream, "+%d (0x%08x)", expression->offset, (unsigned int)expression->offset);
  else {
    /* An offset from -10 to 10 is not printed. */
  }

  if (base_register != -1 && expression->symbol != nullptr &&
      (expression->offset > 10 || expression->offset < -10)) {
    if (expression->offset == 0 && base_register != 0) ss_printf(stream, "+0");

    if (expression->offset != 0 || base_register != 0)
      ss_printf(stream, "($%s)", int_reg_names[base_register]);
  }
}

/* Return true if the EXPRESSION is a constant 0. */

bool is_zero_imm(const imm_expr* expression) {
  return (expression->offset == 0 && expression->symbol == nullptr);
}

/* Return an address expression of the form SYMBOL +/- IOFFSET (REGISTER).
   Any of the three parts may be omitted. */

addr_expr* make_addr_expr(int offset, const char* symbol, int register_no) {
  addr_expr* expression = (addr_expr*)xmalloc(sizeof(addr_expr));
  label* looked_up;

  if (register_no == 0 && symbol != nullptr &&
      (looked_up = lookup_label(symbol))->global_pointer_flag) {
    *expression = (addr_expr){
        .register_no = REG_GP,
        .immediate = make_imm_expr((int)(offset + looked_up->address - gp_midpoint),
                             nullptr, false),
    };
  } else {
    /* make_imm_expr only reads `sym` (via lookup_label) and stores the
       resulting label*, never the string itself — so it neither retains
       nor frees it.  strdup'ing here produced a copy that was orphaned on
       every call; pass the caller's `sym` straight through instead.  The
       caller keeps ownership and frees it. */
    *expression = (addr_expr){
        .register_no = (uint8_t)register_no,
        .immediate = make_imm_expr(offset, symbol, false),
    };
  }
  return (expression);
}

imm_expr* addr_expr_imm(const addr_expr* expression) { return (expression->immediate); }

int addr_expr_reg(const addr_expr* expression) { return (expression->register_no); }

/* Map between a SPIM instruction and the binary representation of the
   instruction. */

#define REGS(R, O) (((R) & 0x1f) << O)

int32_t inst_encode(const mips_instruction* instruction) {
  if (instruction == nullptr) return (0);

  const name_val_val* entry = map_int_to_name_val_val(
      opcode_encoding_tbl, OPCODE_COUNT, OPCODE(instruction));
  if (entry == nullptr) return 0;

  const int32_t table_opcode = entry->value2;
  entry =
      map_int_to_name_val_val(opcode_tbl, OPCODE_COUNT, OPCODE(instruction));

  switch (entry->value2) {
    case BRANCH_COPROC_COND_INST:
      return (table_opcode | REGS(CC(instruction) << 2, 16) |
              (IOFFSET(instruction) & 0xffff));

    case BRANCH_RS_LABEL_INST:
      return (table_opcode | REGS(RS(instruction), 21) |
              (IOFFSET(instruction) & 0xffff));

    case RS_IMM_INST:
      return (table_opcode | REGS(RS(instruction), 21) |
              (IMM(instruction) & 0xffff));

    case RT_IMM_INST:
      return (table_opcode | REGS(RS(instruction), 21) | REGS(RT(instruction), 16) |
              (IMM(instruction) & 0xffff));

    case RT_RS_IMM_INST:
    case BRANCH_RS_RT_LABEL_INST:
      return (table_opcode | REGS(RS(instruction), 21) | REGS(RT(instruction), 16) |
              (IMM(instruction) & 0xffff));

    case RT_ADDR_INST:
      return (table_opcode | REGS(BASE(instruction), 21) |
              REGS(RT(instruction), 16) | (IOFFSET(instruction) & 0xffff));

    case RS_INST:
      return (table_opcode | REGS(RS(instruction), 21));

    case RD_INST:
      return (table_opcode | REGS(RD(instruction), 11));

    case RT_COPREG_INST:
      return (table_opcode | REGS(RT(instruction), 16) | REGS(RD(instruction), 11));

    case RS_RT_INST:
      return (table_opcode | REGS(RS(instruction), 21) | REGS(RT(instruction), 16));

    case RD_RS_INST:
      return (table_opcode | REGS(RS(instruction), 21) | REGS(RD(instruction), 11));

    case RD_RT_SHAMT_INST:
      return (table_opcode | REGS(RT(instruction), 16) | REGS(RD(instruction), 11) |
              REGS(SHAMT(instruction), 6));

    case RD_RS_RT_INST:
      return (table_opcode | REGS(RS(instruction), 21) | REGS(RT(instruction), 16) |
              REGS(RD(instruction), 11));

    case RD_RT_RS_INST:
      return (table_opcode | REGS(RS(instruction), 21) | REGS(RT(instruction), 16) |
              REGS(RD(instruction), 11));

    case FP_FT_ADDR_INST:
      return (table_opcode | REGS(BASE(instruction), 21) |
              REGS(RT(instruction), 16) | (IOFFSET(instruction) & 0xffff));

    case FP_FD_FS_INST:
      return (table_opcode | REGS(FS(instruction), 11) | REGS(FD(instruction), 6));

    case FP_RT_COPREG_INST:
      return (table_opcode | REGS(RT(instruction), 16) | REGS(FS(instruction), 11));

    case FP_CMP_FS_FT_INST:
      return (table_opcode | REGS(FT(instruction), 16) | REGS(FS(instruction), 11) |
              REGS(FD(instruction), 6) | COND(instruction));

    case FP_FD_FS_FT_INST:
      return (table_opcode | REGS(FT(instruction), 16) | REGS(FS(instruction), 11) |
              REGS(FD(instruction), 6));

    case MOVC_TYPE_INST:
      return (table_opcode | REGS(RS(instruction), 21) | REGS(RT(instruction), 16) |
              REGS(RD(instruction), 11));

    case FP_MOVC_TYPE_INST:
      /* The whole rt field: a GPR for movn/movz, cc<<2 for movf/movt (whose tf
         bit is already in table_opcode). */
      return (table_opcode | REGS(RT(instruction), 16) | REGS(FS(instruction), 11) |
              REGS(FD(instruction), 6));

    case J_TYPE_INST:
      return ((int32_t)((mem_addr)table_opcode | TARGET(instruction)));

    case NOARG_TYPE_INST:
      return (table_opcode);

    default:
      fatal_error("Unknown instruction type in inst_encoding\n");
  }
}

mips_instruction* inst_decode(int32_t value) {
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

  const name_val_val* const entry =
      map_int_to_name_val_val(opcode_decode_tbl, OPCODE_DECODE_COUNT, table_opcode);
  if (entry == nullptr)
    return (mk_r_inst(value, 0, 0, 0, 0, 0)); /* Invalid instruction */

  const int32_t instruction_opcode = entry->value2;

  switch (map_int_to_name_val_val(opcode_tbl, OPCODE_COUNT, instruction_opcode)->value2) {
    case BRANCH_COPROC_COND_INST:
      return (mk_i_inst(value, instruction_opcode, BIN_RS(value), BIN_RT(value), value & 0xffff));

    case BRANCH_RS_LABEL_INST:
      return (mk_i_inst(value, instruction_opcode, BIN_RS(value), 0, value & 0xffff));

    case RS_IMM_INST:
      return (mk_i_inst(value, instruction_opcode, BIN_RS(value), 0, value & 0xffff));

    case RT_IMM_INST:
      return (mk_i_inst(value, instruction_opcode, BIN_RS(value), BIN_RT(value), value & 0xffff));

    case RT_RS_IMM_INST:
    case BRANCH_RS_RT_LABEL_INST:
      return (mk_i_inst(value, instruction_opcode, BIN_RS(value), BIN_RT(value), value & 0xffff));

    case RT_ADDR_INST:
      return (mk_i_inst(value, instruction_opcode, BIN_RS(value), BIN_RT(value), value & 0xffff));

    case RS_INST:
      return (mk_r_inst(value, instruction_opcode, BIN_RS(value), 0, 0, 0));

    case RD_INST:
      return (mk_r_inst(value, instruction_opcode, 0, 0, BIN_RD(value), 0));

    case RT_COPREG_INST:
      return (mk_r_inst(value, instruction_opcode, 0, BIN_RT(value), BIN_RD(value), 0));

    case RS_RT_INST:
      return (mk_r_inst(value, instruction_opcode, BIN_RS(value), BIN_RT(value), 0, 0));

    case RD_RS_INST:
      return (mk_r_inst(value, instruction_opcode, BIN_RS(value), 0, BIN_RD(value), 0));

    case RD_RT_SHAMT_INST:
      return (mk_r_inst(value, instruction_opcode, 0, BIN_RT(value), BIN_RD(value),
                        BIN_SHAMT(value)));

    case RD_RS_RT_INST:
      return (
          mk_r_inst(value, instruction_opcode, BIN_RS(value), BIN_RT(value), BIN_RD(value), 0));

    case RD_RT_RS_INST:
      return (
          mk_r_inst(value, instruction_opcode, BIN_RS(value), BIN_RT(value), BIN_RD(value), 0));

    case FP_FT_ADDR_INST:
      return (
          mk_i_inst(value, instruction_opcode, BIN_BASE(value), BIN_FT(value), value & 0xffff));

    case FP_FD_FS_INST:
      return (mk_co_r_inst(value, instruction_opcode, BIN_FS(value), 0, BIN_FD(value)));

    case FP_RT_COPREG_INST:
      return (mk_r_inst(value, instruction_opcode, 0, BIN_RT(value), BIN_FS(value), 0));

    case FP_CMP_FS_FT_INST: {
      /* mk_co_r_inst, not mk_r_inst: FS lives in the rd slot, so passing FD as
         rd would overwrite it.  The condition goes in the rs slot. */
      mips_instruction* instruction =
          mk_co_r_inst(value, instruction_opcode, BIN_FS(value), BIN_FT(value), BIN_FD(value));
      SET_COND(instruction, value & 0xf);
      return (instruction);
    }

    case FP_FD_FS_FT_INST:
      return (
          mk_co_r_inst(value, instruction_opcode, BIN_FS(value), BIN_FT(value), BIN_FD(value)));

    case MOVC_TYPE_INST:
      return (
          mk_r_inst(value, instruction_opcode, BIN_RS(value), BIN_RT(value), BIN_RD(value), 0));

    case FP_MOVC_TYPE_INST:
      /* FS in the rd slot, the GPR (movn/movz) or cc<<2|tf (movf/movt) in rt,
         FD in shamt -- the layout emit_fp_r builds. */
      return (
          mk_co_r_inst(value, instruction_opcode, BIN_FS(value), BIN_RT(value), BIN_FD(value)));

    case J_TYPE_INST:
      return (mk_j_inst(value, instruction_opcode, value & 0x3ffffff));

    case NOARG_TYPE_INST:
      return (mk_r_inst(value, instruction_opcode, 0, 0, 0, 0));

    default:
      return (mk_r_inst(value, 0, 0, 0, 0, 0)); /* Invalid instruction */
  }
}

/* The decode constructors below keep zmalloc + the SET_* accessors rather
   than a designated initializer: the accessors are the one place that maps
   MIPS field names onto mips_instruction's overlapping unions (FS lives in
   the rd slot, FD in shamt, IOFFSET in imm), and an initializer would
   restate that mapping at every site.  zmalloc supplies the zero fill. */

static mips_instruction* mk_r_inst(int32_t value, int opcode, int source_register, int target_register,
                                   int destination_register, int shift_amount) {
  mips_instruction* instruction =
      (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_RS(instruction, source_register);
  SET_RT(instruction, target_register);
  SET_RD(instruction, destination_register);
  SET_SHAMT(instruction, shift_amount);
  SET_ENCODING(instruction, value);
  SET_EXPR(instruction, nullptr);
  return (instruction);
}

static mips_instruction* mk_co_r_inst(int32_t value, int opcode, int floating_point_source, int floating_point_target,
                                      int floating_point_destination) {
  mips_instruction* instruction =
      (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_FS(instruction, floating_point_source);
  SET_FT(instruction, floating_point_target);
  SET_FD(instruction, floating_point_destination);
  SET_ENCODING(instruction, value);
  SET_EXPR(instruction, nullptr);
  return (instruction);
}

static mips_instruction* mk_i_inst(int32_t value, int opcode, int source_register, int target_register,
                                   int offset) {
  mips_instruction* instruction =
      (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_RS(instruction, source_register);
  SET_RT(instruction, target_register);
  SET_IOFFSET(instruction, offset);
  SET_ENCODING(instruction, value);
  SET_EXPR(instruction, nullptr);
  return (instruction);
}

static mips_instruction* mk_j_inst(int32_t value, int opcode, int target) {
  mips_instruction* instruction =
      (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_TARGET(instruction, target);
  SET_ENCODING(instruction, value);
  SET_EXPR(instruction, nullptr);
  return (instruction);
}

/* Code to test encode/decode of instructions. */

void test_assembly(mips_instruction* instruction) {
  mips_instruction* const new_instruction = inst_decode(inst_encode(instruction));

  inst_cmp(instruction, new_instruction);
  free_inst(new_instruction);
}

static void inst_cmp(mips_instruction* instruction1, mips_instruction* instruction2) {
  static str_stream stream;

  ss_clear(&stream);
  if (instruction1->opcode != instruction2->opcode || instruction1->encoding != instruction2->encoding ||
      RS(instruction1) != RS(instruction2) || RT(instruction1) != RT(instruction2) ||
      RD(instruction1) != RD(instruction2) || SHAMT(instruction1) != SHAMT(instruction2)) {
    ss_printf(&stream, "=================== Not Equal ===================\n");
    format_an_inst(&stream, instruction1, 0);
    ss_printf(&stream, "===================\n");
    format_an_inst(&stream, instruction2, 0);
    ss_printf(&stream, "=================== Not Equal ===================\n");
    error("%s", ss_to_string(&stream));
  }
}
