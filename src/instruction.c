
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
#include "data.h"
#include "assembler-event.h"

/* Local functions: */

static imm_expr* copy_imm_expr(imm_expr* old_expr);
static void increment_text_pc(int delta);
static imm_expr* lower_bits_of_expr(imm_expr* old_expr);
static void store_instruction(mips_instruction* instruction);
static imm_expr* upper_bits_of_expr(imm_expr* old_expr);

static int compare_pair_value(const void* a, const void* b);
static void format_imm_expr(str_stream* ss, imm_expr* expr, int base_reg);
static void i_type_inst_full_word(int opcode, int rt, int rs, imm_expr* expr,
                                  int value_known, int32_t value);
static void inst_cmp(mips_instruction* inst1, mips_instruction* inst2);
static mips_instruction* make_r_type_inst(int opcode, int rd, int rs, int rt);
static mips_instruction* mk_i_inst(int32_t value, int opcode, int rs, int rt,
                              int offset);
static mips_instruction* mk_j_inst(int32_t value, int opcode, int target);
static mips_instruction* mk_r_inst(int32_t value, int opcode, int rs, int rt, int rd,
                              int shamt);
static mips_instruction* mk_co_r_inst(int32_t value, int opcode, int fd, int fs,
                                 int ft);
static void produce_immediate(imm_expr* expr, int rt, int value_known,
                              int32_t value);
static void sort_a_opcode_table(void);
static void sort_i_opcode_table(void);
static void sort_name_table(void);

/* Local variables: */

/* True means store instructions in kernel, not user, text segment */

static bool in_kernel = 0;

/* Instruction used as breakpoint by SPIM: */

static mips_instruction* break_inst = nullptr;

/* Locations for next instruction in user and kernel text segments */

static mem_addr next_text_pc;

static mem_addr next_k_text_pc;

static bool enable_text_auto_alignment =
    true; /* => align literal to natural bound */

#define INST_PC (in_kernel ? next_k_text_pc : next_text_pc)

/* Set ADDRESS at which the next instruction is stored. */

void text_begins_at_point(mem_addr addr) {
  next_text_pc = addr;
  in_kernel = false;
  enable_text_auto_alignment = true;
}

void k_text_begins_at_point(mem_addr addr) {
  next_k_text_pc = addr;
  in_kernel = false;
  enable_text_auto_alignment = true;
}

/* Arrange that the next instruction is stored on a memory boundary with its
   low ALIGNMENT bits equal to 0.  If argument is 0, disable automatic
   alignment.*/

void align_text(int alignment) {
  if (alignment == 0)
    enable_text_auto_alignment = false;
  else if (in_kernel) {
    next_k_text_pc =
        (next_k_text_pc + (1 << alignment) - 1) & (0xffffffff << alignment);
    fix_current_label_address(next_k_text_pc);
  } else {
    next_text_pc =
        (next_text_pc + (1 << alignment) - 1) & (0xffffffff << alignment);
    fix_current_label_address(next_text_pc);
  }
}

/* Set the location (in user or kernel text space) for the next instruction. */

void set_text_pc(mem_addr addr) {
  if (in_kernel)
    next_k_text_pc = addr;
  else
    next_text_pc = addr;
}

/* Return address for next instruction, in appropriate text segment. */

mem_addr current_text_pc(void) { return (INST_PC); }

/* Increment the current text segement PC. */

static void increment_text_pc(int delta) {
  if (in_kernel) {
    next_k_text_pc += delta;
    if (k_text_top <= next_k_text_pc)
      run_error("Can't expand kernel text segment\n");
  } else {
    next_text_pc += delta;
    if (text_top <= next_text_pc) run_error("Can't expand text segment\n");
  }
}

/* If FLAG is true, next instruction goes to kernel text segment,
   otherwise it goes to user segment. */

void user_kernel_text_segment(bool to_kernel) { in_kernel = to_kernel; }

/* Store an INSTRUCTION in memory at the next location. */

static void store_instruction(mips_instruction* instruction) {
  if (data_dir) {
    store_word(inst_encode(instruction));
    free_inst(instruction);
  } else if (text_dir) {
    mem_addr at = INST_PC;
    exception_occurred = 0;
    mem_write_inst(at, instruction);
    if (exception_occurred)
      error("Invalid address (0x%08x) for instruction\n", at);
    else
      increment_text_pc(BYTES_PER_WORD);
    if (instruction != nullptr) {
      SET_SOURCE(instruction, source_line());
      if (ENCODING(instruction) == 0) SET_ENCODING(instruction, inst_encode(instruction));
      asm_fire_text_inst(at, (uint32_t)ENCODING(instruction));
    }
  }
}

void i_type_inst_free(int opcode, int rt, int rs, imm_expr* expr) {
  i_type_inst(opcode, rt, rs, expr);
  free(expr);
}

/* Produce an immediate instruction with the OPCODE, RT, RS, and IMM
   fields.  NB, because the immediate value may not fit in the field,
   this routine may produce more than one instruction.	On the bare
   machine, we resolve symbolic address, but they better produce values
   that fit into instruction's immediate field. */

void i_type_inst(int opcode, int rt, int rs, imm_expr* expr) {
  mips_instruction* instruction = (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_RS(instruction, rs);
  SET_RT(instruction, rt);
  SET_EXPR(instruction, copy_imm_expr(expr));
  if (expr->symbol == nullptr || SYMBOL_IS_DEFINED(expr->symbol)) {
    /* Evaluate the instruction's expression. */
    int32_t value = eval_imm_expr(expr);

    if (!bare_machine && (((opcode == TOK_ADDI_OPCODE || opcode == TOK_ADDIU_OPCODE ||
                            opcode == TOK_SLTI_OPCODE || opcode == TOK_SLTIU_OPCODE ||
                            opcode == TOK_TEQI_OPCODE || opcode == TOK_TGEI_OPCODE ||
                            opcode == TOK_TGEIU_OPCODE || opcode == TOK_TLTI_OPCODE ||
                            opcode == TOK_TLTIU_OPCODE || opcode == TOK_TNEI_OPCODE ||
                            (opcode_is_load_store(opcode) && expr->bits == 0))
                               // Sign-extended immediate values:
                               ? ((value & 0xffff8000) != 0 &&
                                  (value & 0xffff8000) != 0xffff8000)
                               // Not sign-extended:
                               : (value & 0xffff0000) != 0))) {
      // Non-immediate value
      free_inst(instruction);
      i_type_inst_full_word(opcode, rt, rs, expr, 1, value);
      return;
    } else
      resolve_a_label(expr->symbol, instruction);
  } else if (bare_machine || expr->bits != 0)
    /* Don't know expression's value, but only needed upper/lower 16-bits
       anyways. */
    record_inst_uses_symbol(instruction, expr->symbol);
  else {
    /* Don't know the expressions's value and want all of its bits,
       so assume that it will not produce a small result and generate
       sequence for 32 bit value. */
    free_inst(instruction);

    i_type_inst_full_word(opcode, rt, rs, expr, 0, 0);
    return;
  }

  store_instruction(instruction);
}

/* The immediate value for an instruction will (or may) not fit in 16 bits.
   Build the value from its piece with separate instructions. */

static void i_type_inst_full_word(int opcode, int rt, int rs, imm_expr* expr,
                                  int value_known, int32_t value) {
  if (opcode_is_load_store(opcode)) {
    int32_t offset;

    if (expr->symbol != nullptr && expr->symbol->gp_flag && rs == 0 &&
        IMM_MIN <= (offset = expr->symbol->addr + expr->offset) &&
        offset <= IMM_MAX) {
      i_type_inst_free(opcode, rt, REG_GP,
                       make_imm_expr(offset, nullptr, false));
    } else if (value_known) {
      int low, high;

      high = (value >> 16) & 0xffff;
      low = value & 0xffff;

      if (!(high == 0 && !(low & 0x8000)) &&
          !(high == 0xffff && (low & 0x8000))) {
        /* Some of high 16 bits are non-zero */
        if (low & 0x8000) {
          /* Adjust high 16, since load sign-extends low 16*/
          high += 1;
        }

        i_type_inst_free(TOK_LUI_OPCODE, 1, 0, const_imm_expr(high));
        if (rs != 0) /* Base register */
        {
          r_type_inst(TOK_ADDU_OPCODE, 1, 1, rs);
        }
        i_type_inst_free(opcode, rt, 1,
                         lower_bits_of_expr(const_imm_expr(low)));
      } else {
        /* Special case, sign-extension of low 16 bits sets high to 0xffff */
        i_type_inst_free(opcode, rt, rs, const_imm_expr(low));
      }
    } else {
      /* Use $at */
      /* Need to adjust if lower bits are negative */
      i_type_inst_free(TOK_LUI_OPCODE, 1, 0, upper_bits_of_expr(expr));
      if (rs != 0) /* Base register */
      {
        r_type_inst(TOK_ADDU_OPCODE, 1, 1, rs);
      }
      i_type_inst_free(opcode, rt, 1, lower_bits_of_expr(expr));
    }
  } else if (opcode_is_branch(opcode)) {
    /* This only allows branches +/- 32K, which is not correct! */
    i_type_inst_free(opcode, rt, rs, lower_bits_of_expr(expr));
  } else
  /* Computation instruction */
  {
    int offset;

    if (expr->symbol != nullptr && expr->symbol->gp_flag && rs == 0 &&
        IMM_MIN <= (offset = expr->symbol->addr + expr->offset) &&
        offset <= IMM_MAX) {
      i_type_inst_free((opcode == TOK_LUI_OPCODE ? TOK_ADDIU_OPCODE : opcode), rt,
                       REG_GP, make_imm_expr(offset, nullptr, false));
    } else {
      /* Use $at */
      if ((opcode == TOK_ORI_OPCODE || opcode == TOK_ADDI_OPCODE ||
           opcode == TOK_ADDIU_OPCODE || opcode == TOK_LUI_OPCODE) &&
          rs == 0) {
        produce_immediate(expr, rt, value_known, value);
      } else {
        produce_immediate(expr, 1, value_known, value);
        r_type_inst(imm_op_to_op(opcode), rt, rs, 1);
      }
    }
  }
}

static void produce_immediate(imm_expr* expr, int rt, int value_known,
                              int32_t value) {
  if (value_known && (value & 0xffff) == 0) {
    i_type_inst_free(TOK_LUI_OPCODE, rt, 0, upper_bits_of_expr(expr));
  } else if (value_known && (value & 0xffff0000) == 0) {
    i_type_inst_free(TOK_ORI_OPCODE, rt, 0, lower_bits_of_expr(expr));
  } else {
    i_type_inst_free(TOK_LUI_OPCODE, 1, 0, upper_bits_of_expr(expr));
    i_type_inst_free(TOK_ORI_OPCODE, rt, 1, lower_bits_of_expr(expr));
  }
}

/* Return a jump-type instruction with the given OPCODE and TARGET
   fields. NB, even the immediate value may not fit in the field, this
   routine will not produce more than one instruction. */

void j_type_inst(int opcode, imm_expr* target) {
  mips_instruction* instruction = (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  target->offset = 0; /* Not PC relative */
  target->pc_relative = false;
  SET_EXPR(instruction, copy_imm_expr(target));
  if (target->symbol == nullptr || SYMBOL_IS_DEFINED(target->symbol))
    resolve_a_label(target->symbol, instruction);
  else
    record_inst_uses_symbol(instruction, target->symbol);
  store_instruction(instruction);
}

/* Return a register-type instruction with the given OPCODE, RD, RS, and RT
   fields. */

static mips_instruction* make_r_type_inst(int opcode, int rd, int rs, int rt) {
  mips_instruction* instruction = (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_RS(instruction, rs);
  SET_RT(instruction, rt);
  SET_RD(instruction, rd);
  SHAMT(instruction) = 0;
  return (instruction);
}

/* Return a register-type instruction with the given OPCODE, RD, RS, and RT
   fields. */

void r_type_inst(int opcode, int rd, int rs, int rt) {
  store_instruction(make_r_type_inst(opcode, rd, rs, rt));
}

/* Return a register-type instruction with the given OPCODE, FD, FS, and FT
   fields. */

void r_co_type_inst(int opcode, int fd, int fs, int ft) {
  mips_instruction* instruction = make_r_type_inst(opcode, fs, 0, ft);
  SET_FD(instruction, fd);
  store_instruction(instruction);
}

/* Return a register-shift instruction with the given OPCODE, RD, RT, and
   SHAMT fields.*/

void r_sh_type_inst(int opcode, int rd, int rt, int shamt) {
  mips_instruction* instruction = make_r_type_inst(opcode, rd, 0, rt);
  SET_SHAMT(instruction, shamt & 0x1f);
  store_instruction(instruction);
}

/* Return a floating-point compare instruction with the given OPCODE,
   FS, FT, and CC fields.*/

void r_cond_type_inst(int opcode, int fs, int ft, int cc) {
  mips_instruction* instruction = make_r_type_inst(opcode, fs, 0, ft);
  SET_FD(instruction, cc << 2);
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
  }
  store_instruction(instruction);
}

/* Make and return a deep copy of INST. */

mips_instruction* copy_inst(mips_instruction* instruction) {
  mips_instruction* new_inst = (mips_instruction*)xmalloc(sizeof(mips_instruction));

  *new_inst = *instruction;
  /*memcpy ((void*)new_inst, (void*)instruction , sizeof (instruction));*/
  SET_EXPR(new_inst, copy_imm_expr(EXPR(instruction)));
  return (new_inst);
}

void free_inst(mips_instruction* instruction) {
  if (instruction != break_inst)
  /* Don't free the breakpoint insructions since we only have one. */
  {
    if (EXPR(instruction)) free(EXPR(instruction));
    free(instruction);
  }
}

/* Maintain a table mapping from opcode to instruction name and
   instruction type.

   Table must be sorted before first use since its entries are
   alphabetical on name, not ordered by opcode. */

/* Sort all instruction table before first use. */

void initialize_inst_tables(void) {
  sort_name_table();
  sort_i_opcode_table();
  sort_a_opcode_table();
}

/* Map from token -> (name, type).  Used by inst_op_name() and similar
   to render an opcode back to its mnemonic string at disassembly time.

   First of three op.h inclusions in this file.  Each one defines OP()
   differently to extract the columns that table needs from the same
   380-row instruction list.  See op.h's top-of-file comment for the
   X-macro pattern. */

static name_val_val name_tbl[] = {
#undef OP
#define OP(NAME, OPCODE, TYPE, R_OPCODE) {NAME, OPCODE, TYPE},
#include "opcodes.h"
};

/* Sort the opcode table on their key (the opcode value). */

static void sort_name_table(void) {
  qsort(name_tbl, sizeof(name_tbl) / sizeof(name_val_val), sizeof(name_val_val),
        compare_pair_value);
}

/* Compare the VALUE1 field of two NAME_VAL_VAL entries in the format
   required by qsort. */

static int compare_pair_value(const void* a, const void* b) {
  const name_val_val* p1 = a;
  const name_val_val* p2 = b;
  if (p1->value1 < p2->value1)
    return (-1);
  else if (p1->value1 > p2->value1)
    return (1);
  else
    return (0);
}

/* Print the instruction stored at the memory ADDRESS. */

void print_inst(mem_addr addr) {
  char* inst_str = inst_to_string(addr);
  write_output(message_out, "%s", inst_str);
  free(inst_str);
}

/* Return the canonical mnemonic for instruction (e.g. "addu", "ori", "j"), or
   "<unknown>" if OPCODE(instruction) isn't in name_tbl. The returned pointer is
   owned by the static name_tbl and must not be freed. */
const char* inst_op_name(mips_instruction* instruction) {
  if (instruction == nullptr) return "<null>";
  name_val_val* entry = map_int_to_name_val_val(
      name_tbl, sizeof(name_tbl) / sizeof(name_val_val), OPCODE(instruction));
  return entry ? entry->name : "<unknown>";
}

/* Op-token variant: take a raw opcode token (TOK_*_OP, TOK_*_POP) and
   return its mnemonic — used by the AST builder to label AST_PSEUDO
   nodes. */
const char* op_token_name(int op_token) {
  name_val_val* entry = map_int_to_name_val_val(
      name_tbl, sizeof(name_tbl) / sizeof(name_val_val), op_token);
  return entry ? entry->name : "<unknown>";
}

char* inst_to_string(mem_addr addr) {
  str_stream ss;
  mips_instruction* instruction;

  exception_occurred = 0;
  instruction = mem_read_inst(addr);

  if (exception_occurred) {
    error("Can't print instruction not in text segment (0x%08x)\n", addr);
    return "";
  }

  ss_init(&ss);
  format_an_inst(&ss, instruction, addr);
  return ss_to_string(&ss);
}

void format_an_inst(str_stream* ss, mips_instruction* instruction, mem_addr addr) {
  name_val_val* entry;
  int line_start = ss_length(ss);

  if (addr != 0 && inst_is_breakpoint(addr)) {
    delete_breakpoint(addr);
    ss_printf(ss, "*");
    format_an_inst(ss, mem_read_inst(addr), addr);
    add_breakpoint(addr);
    return;
  }

  ss_printf(ss, "[0x%08x]\t", addr);
  if (instruction == nullptr) {
    ss_printf(ss, "<none>\n");
    return;
  }

  entry = map_int_to_name_val_val(
      name_tbl, sizeof(name_tbl) / sizeof(name_val_val), OPCODE(instruction));
  if (entry == nullptr) {
    ss_printf(ss, "<unknown instruction %d>\n", OPCODE(instruction));
    return;
  }

  ss_printf(ss, "0x%08x  %s", (uint32_t)ENCODING(instruction), entry->name);
  switch (entry->value2) {
    case BC_TYPE_INST:
      ss_printf(ss, "%d %d", CC(instruction), BRANCH_OFFSET(instruction));
      break;

    case B1_TYPE_INST:
      ss_printf(ss, " $%s %d", int_reg_names[RS(instruction)], BRANCH_OFFSET(instruction));
      break;

    case I1s_TYPE_INST:
      ss_printf(ss, " $%s, %d", int_reg_names[RS(instruction)], IMM(instruction));
      break;

    case I1t_TYPE_INST:
      ss_printf(ss, " $%s, %d", int_reg_names[RT(instruction)], IMM(instruction));
      break;

    case I2_TYPE_INST:
      ss_printf(ss, " $%s, $%s, %d", int_reg_names[RT(instruction)],
                int_reg_names[RS(instruction)], IMM(instruction));
      break;

    case B2_TYPE_INST:
      ss_printf(ss, " $%s, $%s, %d", int_reg_names[RS(instruction)],
                int_reg_names[RT(instruction)], BRANCH_OFFSET(instruction));
      break;

    case I2a_TYPE_INST:
      ss_printf(ss, " $%s, %d($%s)", int_reg_names[RT(instruction)], IMM(instruction),
                int_reg_names[BASE(instruction)]);
      break;

    case R1s_TYPE_INST:
      ss_printf(ss, " $%s", int_reg_names[RS(instruction)]);
      break;

    case R1d_TYPE_INST:
      ss_printf(ss, " $%s", int_reg_names[RD(instruction)]);
      break;

    case R2td_TYPE_INST:
      ss_printf(ss, " $%s, $%s", int_reg_names[RT(instruction)],
                int_reg_names[RD(instruction)]);
      break;

    case R2st_TYPE_INST:
      ss_printf(ss, " $%s, $%s", int_reg_names[RS(instruction)],
                int_reg_names[RT(instruction)]);
      break;

    case R2ds_TYPE_INST:
      ss_printf(ss, " $%s, $%s", int_reg_names[RD(instruction)],
                int_reg_names[RS(instruction)]);
      break;

    case R2sh_TYPE_INST:
      /* nop is canonically sll $0, $0, 0. ENCODING() is unreliable here
       * (the assembler doesn't populate it for source-assembled instructions,
       * so it's always 0 and would falsely make every shift print as nop).
       * Check the actual fields instead. */
      if (RD(instruction) == 0 && RT(instruction) == 0 && SHAMT(instruction) == 0) {
        ss_erase(ss, 3); /* zap sll */
        ss_printf(ss, "nop");
      } else
        ss_printf(ss, " $%s, $%s, %d", int_reg_names[RD(instruction)],
                  int_reg_names[RT(instruction)], SHAMT(instruction));
      break;

    case R3_TYPE_INST:
      ss_printf(ss, " $%s, $%s, $%s", int_reg_names[RD(instruction)],
                int_reg_names[RS(instruction)], int_reg_names[RT(instruction)]);
      break;

    case R3sh_TYPE_INST:
      ss_printf(ss, " $%s, $%s, $%s", int_reg_names[RD(instruction)],
                int_reg_names[RT(instruction)], int_reg_names[RS(instruction)]);
      break;

    case FP_I2a_TYPE_INST:
      ss_printf(ss, " $f%d, %d($%s)", FT(instruction), IMM(instruction),
                int_reg_names[BASE(instruction)]);
      break;

    case FP_R2ds_TYPE_INST:
      ss_printf(ss, " $f%d, $f%d", FD(instruction), FS(instruction));
      break;

    case FP_R2ts_TYPE_INST:
      ss_printf(ss, " $%s, $f%d", int_reg_names[RT(instruction)], FS(instruction));
      break;

    case FP_CMP_TYPE_INST:
      if (FD(instruction) == 0)
        ss_printf(ss, " $f%d, $f%d", FS(instruction), FT(instruction));
      else
        ss_printf(ss, " %d, $f%d, $f%d", FD(instruction) >> 2, FS(instruction), FT(instruction));
      break;

    case FP_R3_TYPE_INST:
      ss_printf(ss, " $f%d, $f%d, $f%d", FD(instruction), FS(instruction), FT(instruction));
      break;

    case MOVC_TYPE_INST:
      ss_printf(ss, " $%s, $%s, %d", int_reg_names[RD(instruction)],
                int_reg_names[RS(instruction)], RT(instruction) >> 2);
      break;

    case FP_MOVC_TYPE_INST:
      ss_printf(ss, " $f%d, $f%d, %d", FD(instruction), FS(instruction), CC(instruction));
      break;

    case J_TYPE_INST:
      ss_printf(ss, " 0x%08x", TARGET(instruction) << 2);
      break;

    case NOARG_TYPE_INST:
      break;

    default:
      fatal_error("Unknown instruction type in print_inst\n");
  }

  if (EXPR(instruction) != nullptr && EXPR(instruction)->symbol != nullptr) {
    ss_printf(ss, " [");
    if (opcode_is_load_store(OPCODE(instruction)))
      format_imm_expr(ss, EXPR(instruction), BASE(instruction));
    else
      format_imm_expr(ss, EXPR(instruction), -1);
    ss_printf(ss, "]");
  }

  if (SOURCE(instruction) != nullptr) {
    /* Comment is source line text of current line. */
    int gap_length = 57 - (ss_length(ss) - line_start);
    for (; 0 < gap_length; gap_length -= 1) {
      ss_printf(ss, " ");
    }

    ss_printf(ss, "; ");
    ss_printf(ss, "%s", SOURCE(instruction));
  }

  ss_printf(ss, "\n");
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

bool inst_is_breakpoint(mem_addr addr) {
  if (break_inst == nullptr)
    break_inst = make_r_type_inst(TOK_BREAK_OPCODE, 1, 0, 0);

  return (mem_read_inst(addr) == break_inst);
}

/* Set a breakpoint at ADDR and return the old instruction.  If the
   breakpoint cannot be set, return nullptr. */

mips_instruction* set_breakpoint(mem_addr addr) {
  mips_instruction* old_inst;

  if (break_inst == nullptr)
    break_inst = make_r_type_inst(TOK_BREAK_OPCODE, 1, 0, 0);

  exception_occurred = 0;
  old_inst = mem_read_inst(addr);
  if (old_inst == break_inst) return (nullptr);

  mem_write_inst(addr, break_inst);
  if (exception_occurred)
    return (nullptr);
  else
    return (old_inst);
}

/* An immediate expression has the form: SYMBOL +/- IOFFSET, where either
   part may be omitted. */

/* Make and return a new immediate expression */

imm_expr* make_imm_expr(int offs, char* sym, bool is_pc_relative) {
  imm_expr* expr = (imm_expr*)xmalloc(sizeof(imm_expr));

  expr->offset = offs;
  expr->bits = 0;
  expr->pc_relative = is_pc_relative;
  if (sym != nullptr)
    expr->symbol = lookup_label(sym);
  else
    expr->symbol = nullptr;
  return (expr);
}

/* Return a shallow copy of the EXPRESSION. */

static imm_expr* copy_imm_expr(imm_expr* old_expr) {
  imm_expr* expr = (imm_expr*)xmalloc(sizeof(imm_expr));

  *expr = *old_expr;
  /*memcpy ((void*)expr, (void*)old_expr, sizeof (imm_expr));*/
  return (expr);
}

/* Return a shallow copy of an EXPRESSION that only uses the upper
   sixteen bits of the expression's value. */

static imm_expr* upper_bits_of_expr(imm_expr* old_expr) {
  imm_expr* expr = copy_imm_expr(old_expr);

  expr->bits = 1;
  return (expr);
}

/* Return a shallow copy of the EXPRESSION that only uses the lower
   sixteen bits of the expression's value. */

static imm_expr* lower_bits_of_expr(imm_expr* old_expr) {
  imm_expr* expr = copy_imm_expr(old_expr);

  expr->bits = -1;
  return (expr);
}

/* Return an instruction expression for a constant VALUE. */

imm_expr* const_imm_expr(int32_t value) {
  return (make_imm_expr(value, nullptr, false));
}

/* Return a shallow copy of the EXPRESSION with the offset field
   incremented by the given amount. */

imm_expr* incr_expr_offset(imm_expr* expr, int32_t value) {
  imm_expr* new_expr = copy_imm_expr(expr);

  new_expr->offset += value;
  return (new_expr);
}

/* Return the value of the EXPRESSION. */

int32_t eval_imm_expr(imm_expr* expr) {
  int32_t value;

  if (expr->symbol == nullptr)
    value = expr->offset;
  else if (SYMBOL_IS_DEFINED(expr->symbol)) {
    value = expr->offset + expr->symbol->addr;
  } else {
    error("Evaluated undefined symbol: %s\n", expr->symbol->name);
    value = 0;
  }
  if (expr->bits > 0)
    return ((value >> 16) & 0xffff); /* Use upper bits of result */
  else if (expr->bits < 0)
    return (value & 0xffff); /* Use lower bits */
  else
    return (value);
}

/* Print the EXPRESSION. */

static void format_imm_expr(str_stream* ss, imm_expr* expr, int base_reg) {
  if (expr->symbol != nullptr) {
    ss_printf(ss, "%s", expr->symbol->name);
  }

  if (expr->pc_relative)
    ss_printf(ss, "-0x%08x", (unsigned int)-expr->offset);
  else if (expr->offset < -10)
    ss_printf(ss, "-%d (-0x%08x)", -expr->offset, (unsigned int)-expr->offset);
  else if (expr->offset > 10)
    ss_printf(ss, "+%d (0x%08x)", expr->offset, (unsigned int)expr->offset);

  if (base_reg != -1 && expr->symbol != nullptr &&
      (expr->offset > 10 || expr->offset < -10)) {
    if (expr->offset == 0 && base_reg != 0) ss_printf(ss, "+0");

    if (expr->offset != 0 || base_reg != 0)
      ss_printf(ss, "($%s)", int_reg_names[base_reg]);
  }
}

/* Return true if the EXPRESSION is a constant 0. */

bool is_zero_imm(imm_expr* expr) {
  return (expr->offset == 0 && expr->symbol == nullptr);
}

/* Return an address expression of the form SYMBOL +/- IOFFSET (REGISTER).
   Any of the three parts may be omitted. */

addr_expr* make_addr_expr(int offs, char* sym, int reg_no) {
  addr_expr* expr = (addr_expr*)xmalloc(sizeof(addr_expr));
  label* looked_up;

  if (reg_no == 0 && sym != nullptr && (looked_up = lookup_label(sym))->gp_flag) {
    expr->reg_no = REG_GP;
    expr->imm =
        make_imm_expr(offs + looked_up->addr - gp_midpoint, nullptr, false);
  } else {
    expr->reg_no = (unsigned char)reg_no;
    expr->imm = make_imm_expr(offs, (sym ? str_copy(sym) : sym), false);
  }
  return (expr);
}

imm_expr* addr_expr_imm(addr_expr* expr) { return (expr->imm); }

int addr_expr_reg(addr_expr* expr) { return (expr->reg_no); }

/* Map between a SPIM instruction and the binary representation of the
   instruction. */

/* Maintain a table mapping from internal opcode (i_opcode) to actual
   opcode (a_opcode).  Table must be sorted before first use since its
   entries are alphabetical on name, not ordered by opcode. */

/* Map from internal opcode -> real opcode.

   Second of three op.h inclusions in this file.  This one keeps the
   binary encoding in the third slot — used to look up "what bits do I
   emit for this token?"  See op.h for the X-macro pattern. */

static name_val_val i_opcode_tbl[] = {
#undef OP
#define OP(NAME, I_OPCODE, TYPE, A_OPCODE) {NAME, I_OPCODE, (int)A_OPCODE},
#include "opcodes.h"
};

/* Sort the opcode table on their key (the interal opcode value). */

static void sort_i_opcode_table(void) {
  qsort(i_opcode_tbl, sizeof(i_opcode_tbl) / sizeof(name_val_val),
        sizeof(name_val_val), compare_pair_value);
}

#define REGS(R, O) (((R) & 0x1f) << O)

int32_t inst_encode(mips_instruction* instruction) {
  int32_t a_opcode = 0;
  name_val_val* entry;

  if (instruction == nullptr) return (0);

  entry = map_int_to_name_val_val(
      i_opcode_tbl, sizeof(i_opcode_tbl) / sizeof(name_val_val), OPCODE(instruction));
  if (entry == nullptr) return 0;

  a_opcode = entry->value2;
  entry = map_int_to_name_val_val(
      name_tbl, sizeof(name_tbl) / sizeof(name_val_val), OPCODE(instruction));

  switch (entry->value2) {
    case BC_TYPE_INST:
      return (a_opcode | REGS(CC(instruction) << 2, 16) | (IOFFSET(instruction) & 0xffff));

    case B1_TYPE_INST:
      return (a_opcode | REGS(RS(instruction), 21) | (IOFFSET(instruction) & 0xffff));

    case I1s_TYPE_INST:
      return (a_opcode | REGS(RS(instruction), 21) | (IMM(instruction) & 0xffff));

    case I1t_TYPE_INST:
      return (a_opcode | REGS(RS(instruction), 21) | REGS(RT(instruction), 16) |
              (IMM(instruction) & 0xffff));

    case I2_TYPE_INST:
    case B2_TYPE_INST:
      return (a_opcode | REGS(RS(instruction), 21) | REGS(RT(instruction), 16) |
              (IMM(instruction) & 0xffff));

    case I2a_TYPE_INST:
      return (a_opcode | REGS(BASE(instruction), 21) | REGS(RT(instruction), 16) |
              (IOFFSET(instruction) & 0xffff));

    case R1s_TYPE_INST:
      return (a_opcode | REGS(RS(instruction), 21));

    case R1d_TYPE_INST:
      return (a_opcode | REGS(RD(instruction), 11));

    case R2td_TYPE_INST:
      return (a_opcode | REGS(RT(instruction), 16) | REGS(RD(instruction), 11));

    case R2st_TYPE_INST:
      return (a_opcode | REGS(RS(instruction), 21) | REGS(RT(instruction), 16));

    case R2ds_TYPE_INST:
      return (a_opcode | REGS(RS(instruction), 21) | REGS(RD(instruction), 11));

    case R2sh_TYPE_INST:
      return (a_opcode | REGS(RT(instruction), 16) | REGS(RD(instruction), 11) |
              REGS(SHAMT(instruction), 6));

    case R3_TYPE_INST:
      return (a_opcode | REGS(RS(instruction), 21) | REGS(RT(instruction), 16) |
              REGS(RD(instruction), 11));

    case R3sh_TYPE_INST:
      return (a_opcode | REGS(RS(instruction), 21) | REGS(RT(instruction), 16) |
              REGS(RD(instruction), 11));

    case FP_I2a_TYPE_INST:
      return (a_opcode | REGS(BASE(instruction), 21) | REGS(RT(instruction), 16) |
              (IOFFSET(instruction) & 0xffff));

    case FP_R2ds_TYPE_INST:
      return (a_opcode | REGS(FS(instruction), 11) | REGS(FD(instruction), 6));

    case FP_R2ts_TYPE_INST:
      return (a_opcode | REGS(RT(instruction), 16) | REGS(FS(instruction), 11));

    case FP_CMP_TYPE_INST:
      return (a_opcode | REGS(FT(instruction), 16) | REGS(FS(instruction), 11) |
              REGS(FD(instruction), 6) | COND(instruction));

    case FP_R3_TYPE_INST:
      return (a_opcode | REGS(FT(instruction), 16) | REGS(FS(instruction), 11) |
              REGS(FD(instruction), 6));

    case MOVC_TYPE_INST:
      return (a_opcode | REGS(RS(instruction), 21) | REGS(RT(instruction), 16) |
              REGS(RD(instruction), 11));

    case FP_MOVC_TYPE_INST:
      return (a_opcode | REGS(CC(instruction), 18) | REGS(FS(instruction), 11) |
              REGS(FD(instruction), 6));

    case J_TYPE_INST:
      return (a_opcode | TARGET(instruction));

    case NOARG_TYPE_INST:
      return (a_opcode);

    default:
      fatal_error("Unknown instruction type in inst_encoding\n");
      return (0); /* Not reached */
  }
}

/* Maintain a table mapping from actual opcode to interal opcode.
   Table must be sorted before first use since its entries are
   alphabetical on name, not ordered by opcode. */

/* Map from real opcode -> internal opcode (reverse of i_opcode_tbl).

   Third of three op.h inclusions in this file.  Same row data, fields
   swapped — used to look up "what token decodes this binary
   encoding?" during disassembly.  See op.h for the X-macro pattern. */

static name_val_val a_opcode_tbl[] = {
#undef OP
#define OP(NAME, I_OPCODE, TYPE, A_OPCODE) {NAME, (int)A_OPCODE, (int)I_OPCODE},
#include "opcodes.h"
};

/* Sort the opcode table on their key (the interal opcode value). */

static void sort_a_opcode_table(void) {
  qsort(a_opcode_tbl, sizeof(a_opcode_tbl) / sizeof(name_val_val),
        sizeof(name_val_val), compare_pair_value);
}

mips_instruction* inst_decode(int32_t val) {
  int32_t a_opcode = val & 0xfc000000;
  name_val_val* entry;
  int32_t i_opcode;

  /* Field classes: (opcode is continued in other part of instruction): */
  if (a_opcode == 0 || a_opcode == 0x70000000) /* SPECIAL or SPECIAL2 */
    a_opcode |= (val & 0x3f);
  else if (a_opcode == 0x04000000) /* REGIMM */
    a_opcode |= (val & 0x001f0000);
  else if (a_opcode == 0x40000000) /* COP0 */
    a_opcode |= (val & 0x03e00000) | (val & 0x1f);
  else if (a_opcode == 0x44000000) /* COP1 */
  {
    a_opcode |= (val & 0x03e00000);
    if ((val & 0xff000000) == 0x45000000)
      a_opcode |= (val & 0x00010000); /* BC1f/t */
    else
      a_opcode |= (val & 0x3f);
  } else if (a_opcode == 0x48000000 /* COPz */
             || a_opcode == 0x4c000000)
    a_opcode |= (val & 0x03e00000);

  entry = map_int_to_name_val_val(
      a_opcode_tbl, sizeof(a_opcode_tbl) / sizeof(name_val_val), a_opcode);
  if (entry == nullptr)
    return (mk_r_inst(val, 0, 0, 0, 0, 0)); /* Invalid instruction */

  i_opcode = entry->value2;

  switch (map_int_to_name_val_val(
              name_tbl, sizeof(name_tbl) / sizeof(name_val_val), i_opcode)
              ->value2) {
    case BC_TYPE_INST:
      return (mk_i_inst(val, i_opcode, BIN_RS(val), BIN_RT(val), val & 0xffff));

    case B1_TYPE_INST:
      return (mk_i_inst(val, i_opcode, BIN_RS(val), 0, val & 0xffff));

    case I1s_TYPE_INST:
      return (mk_i_inst(val, i_opcode, BIN_RS(val), 0, val & 0xffff));

    case I1t_TYPE_INST:
      return (mk_i_inst(val, i_opcode, BIN_RS(val), BIN_RT(val), val & 0xffff));

    case I2_TYPE_INST:
    case B2_TYPE_INST:
      return (mk_i_inst(val, i_opcode, BIN_RS(val), BIN_RT(val), val & 0xffff));

    case I2a_TYPE_INST:
      return (mk_i_inst(val, i_opcode, BIN_RS(val), BIN_RT(val), val & 0xffff));

    case R1s_TYPE_INST:
      return (mk_r_inst(val, i_opcode, BIN_RS(val), 0, 0, 0));

    case R1d_TYPE_INST:
      return (mk_r_inst(val, i_opcode, 0, 0, BIN_RD(val), 0));

    case R2td_TYPE_INST:
      return (mk_r_inst(val, i_opcode, 0, BIN_RT(val), BIN_RD(val), 0));

    case R2st_TYPE_INST:
      return (mk_r_inst(val, i_opcode, BIN_RS(val), BIN_RT(val), 0, 0));

    case R2ds_TYPE_INST:
      return (mk_r_inst(val, i_opcode, BIN_RS(val), 0, BIN_RD(val), 0));

    case R2sh_TYPE_INST:
      return (
          mk_r_inst(val, i_opcode, 0, BIN_RT(val), BIN_RD(val), BIN_SHAMT(val)));

    case R3_TYPE_INST:
      return (
          mk_r_inst(val, i_opcode, BIN_RS(val), BIN_RT(val), BIN_RD(val), 0));

    case R3sh_TYPE_INST:
      return (
          mk_r_inst(val, i_opcode, BIN_RS(val), BIN_RT(val), BIN_RD(val), 0));

    case FP_I2a_TYPE_INST:
      return (
          mk_i_inst(val, i_opcode, BIN_BASE(val), BIN_FT(val), val & 0xffff));

    case FP_R2ds_TYPE_INST:
      return (mk_co_r_inst(val, i_opcode, BIN_FS(val), 0, BIN_FD(val)));

    case FP_R2ts_TYPE_INST:
      return (mk_r_inst(val, i_opcode, 0, BIN_RT(val), BIN_FS(val), 0));

    case FP_CMP_TYPE_INST: {
      mips_instruction* instruction =
          mk_r_inst(val, i_opcode, BIN_FS(val), BIN_FT(val), BIN_FD(val), 0);
      SET_COND(instruction, val & 0xf);
      return (instruction);
    }

    case FP_R3_TYPE_INST:
      return (
          mk_co_r_inst(val, i_opcode, BIN_FS(val), BIN_FT(val), BIN_FD(val)));

    case MOVC_TYPE_INST:
      return (
          mk_r_inst(val, i_opcode, BIN_RS(val), BIN_RT(val), BIN_RD(val), 0));

    case FP_MOVC_TYPE_INST:
      return (
          mk_r_inst(val, i_opcode, BIN_FS(val), BIN_RT(val), BIN_FD(val), 0));

    case J_TYPE_INST:
      return (mk_j_inst(val, i_opcode, val & 0x3ffffff));

    case NOARG_TYPE_INST:
      return (mk_r_inst(val, i_opcode, 0, 0, 0, 0));

    default:
      return (mk_r_inst(val, 0, 0, 0, 0, 0)); /* Invalid instruction */
  }
}

static mips_instruction* mk_r_inst(int32_t val, int opcode, int rs, int rt, int rd,
                              int shamt) {
  mips_instruction* instruction = (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_RS(instruction, rs);
  SET_RT(instruction, rt);
  SET_RD(instruction, rd);
  SET_SHAMT(instruction, shamt);
  SET_ENCODING(instruction, val);
  SET_EXPR(instruction, nullptr);
  return (instruction);
}

static mips_instruction* mk_co_r_inst(int32_t val, int opcode, int fs, int ft,
                                 int fd) {
  mips_instruction* instruction = (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_FS(instruction, fs);
  SET_FT(instruction, ft);
  SET_FD(instruction, fd);
  SET_ENCODING(instruction, val);
  SET_EXPR(instruction, nullptr);
  return (instruction);
}

static mips_instruction* mk_i_inst(int32_t val, int opcode, int rs, int rt,
                              int offset) {
  mips_instruction* instruction = (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_RS(instruction, rs);
  SET_RT(instruction, rt);
  SET_IOFFSET(instruction, offset);
  SET_ENCODING(instruction, val);
  SET_EXPR(instruction, nullptr);
  return (instruction);
}

static mips_instruction* mk_j_inst(int32_t val, int opcode, int target) {
  mips_instruction* instruction = (mips_instruction*)zmalloc(sizeof(mips_instruction));

  SET_OPCODE(instruction, opcode);
  SET_TARGET(instruction, target);
  SET_ENCODING(instruction, val);
  SET_EXPR(instruction, nullptr);
  return (instruction);
}

/* Code to test encode/decode of instructions. */

void test_assembly(mips_instruction* instruction) {
  mips_instruction* new_inst = inst_decode(inst_encode(instruction));

  inst_cmp(instruction, new_inst);
  free_inst(new_inst);
}

static void inst_cmp(mips_instruction* inst1, mips_instruction* inst2) {
  static str_stream ss;

  ss_clear(&ss);
  if (inst1->opcode != inst2->opcode || inst1->encoding != inst2->encoding ||
      RS(inst1) != RS(inst2) || RT(inst1) != RT(inst2) ||
      RD(inst1) != RD(inst2) || SHAMT(inst1) != SHAMT(inst2)) {
    ss_printf(&ss, "=================== Not Equal ===================\n");
    format_an_inst(&ss, inst1, 0);
    ss_printf(&ss, "===================\n");
    format_an_inst(&ss, inst2, 0);
    ss_printf(&ss, "=================== Not Equal ===================\n");
    error(ss_to_string(&ss));
  }
}
