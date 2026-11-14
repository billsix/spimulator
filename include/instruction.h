/* SPIM S20 MIPS simulator.
   Description of a SPIM S20 instruction.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

/* Represenation of the expression that produce a value for an instruction's
   immediate field.  Immediates have the form: label +/- offset. */

#ifndef INST_H
#define INST_H

#include "spim.h"
#include "string-stream.h"

typedef struct immediate_expression {
  int32_t offset;                /* Offset from symbol */
  struct label* symbol;          /* Symbolic label */
  int16_t bits;                  /* > 0 => 31..16, < 0 => 15..0 */
  bool program_counter_relative; /* => offset from label in code */
} immediate_expression;

/* Representation of the expression that produce an address for an
   instruction.  Address have the form: label +/- offset (register). */

typedef struct address_expression {
  uint8_t register_no;             /* Register number */
  immediate_expression* immediate; /* The immediate part */
} address_expression;

/* Representation of an instruction. Store the instruction fields in an
   overlapping manner similar to the real encoding (but not identical, to
   speed decoding in C code, as opposed to hardware).. */

typedef struct mips_instruction_struct {
  int16_t opcode;

  union {
    /* R-type or I-type: */
    struct {
      uint8_t source_register;
      uint8_t target_register;

      union {
        int16_t immediate;

        struct {
          uint8_t destination_register;
          uint8_t shift_amount;
        } r_type;
      } immediate_or_r_type;
    } registers;

    /* J-type: */
    memory_address target;
  } fields;

  int32_t encoding;
  immediate_expression* expression;
  char* source_line;
  /* Set on each instruction the assembler emitted for a pseudo-op, or for
     a real instruction it had to expand into more than one (lw from a
     label becomes lui $at + lw): the mnemonic written in the source (a
     static string), this instruction's 1-based position in the
     expansion, how many instructions the line became, and whether the
     mnemonic is a pseudo-op.  Null, 0 and false for an instruction
     written as itself. */
  const char* expansion_of;
  uint8_t expansion_index;
  uint8_t expansion_count;
  bool expansion_of_pseudo;
} mips_instruction;

#define OPCODE(INST) (INST)->opcode
#define SET_OPCODE(INST, VAL) (INST)->opcode = (int16_t)(VAL)

#define SOURCE_REGISTER(INST) (INST)->fields.registers.source_register
#define SET_SOURCE_REGISTER(INST, VAL) \
  (INST)->fields.registers.source_register = (uint8_t)(VAL)

#define TARGET_REGISTER(INST) (INST)->fields.registers.target_register
#define SET_TARGET_REGISTER(INST, VAL) \
  (INST)->fields.registers.target_register = (uint8_t)(VAL)

#define DESTINATION_REGISTER(INST) \
  (INST)->fields.registers.immediate_or_r_type.r_type.destination_register
#define SET_DESTINATION_REGISTER(INST, VAL)                                  \
  (INST)->fields.registers.immediate_or_r_type.r_type.destination_register = \
      (uint8_t)(VAL)

#define FLOATING_POINT_SOURCE(INST) DESTINATION_REGISTER(INST)
#define SET_FLOATING_POINT_SOURCE(INST, VAL) SET_DESTINATION_REGISTER(INST, VAL)

#define FLOATING_POINT_TARGET(INST) TARGET_REGISTER(INST)
#define SET_FLOATING_POINT_TARGET(INST, VAL) SET_TARGET_REGISTER(INST, VAL)

#define FLOATING_POINT_DESTINATION(INST) SHIFT_AMOUNT(INST)
#define SET_FLOATING_POINT_DESTINATION(INST, VAL) SET_SHIFT_AMOUNT(INST, VAL)

#define SHIFT_AMOUNT(INST) \
  (INST)->fields.registers.immediate_or_r_type.r_type.shift_amount
#define SET_SHIFT_AMOUNT(INST, VAL)                                  \
  (INST)->fields.registers.immediate_or_r_type.r_type.shift_amount = \
      (uint8_t)(VAL)

#define IMMEDIATE(INST) (INST)->fields.registers.immediate_or_r_type.immediate
#define SET_IMMEDIATE(INST, VAL) \
  (INST)->fields.registers.immediate_or_r_type.immediate = (int16_t)(VAL)

#define BASE(INST) SOURCE_REGISTER(INST)
#define SET_BASE(INST, VAL) SET_SOURCE_REGISTER(INST, VAL)

#define IMMEDIATE_OFFSET(INST) IMMEDIATE(INST)
#define SET_IMMEDIATE_OFFSET(INST, VAL) SET_IMMEDIATE(INST, VAL)
/* Shift as unsigned: IMMEDIATE_OFFSET is a signed int16_t, and `<< 2` on a
   negative value is UB.  sign_ex() then re-derives the signed offset from the
   low bits.
 */
/* The 16-bit word offset, sign-extended, then scaled to bytes (an 18-bit
   signed byte offset). */
#define BRANCH_OFFSET(INST) \
  ((int32_t)((uint32_t)sign_ex(IMMEDIATE_OFFSET(INST)) << 2))

#define CONDITION(INST) SOURCE_REGISTER(INST)
#define SET_CONDITION(INST, VAL) SET_SOURCE_REGISTER(INST, VAL)

#define CONDITION_CODE(INST) (TARGET_REGISTER(INST) >> 2)
#define NULLIFY_DELAY(INST) ((TARGET_REGISTER(INST) & 0x2) >> 1)
#define TRUE_FALSE(INST) (TARGET_REGISTER(INST) & 0x1)

#define FLOATING_POINT_CONDITION_CODE(INST) \
  (FLOATING_POINT_DESTINATION(INST) >> 2)
#define FLOATING_POINT_NULLIFY_DELAY(INST) \
  ((FLOATING_POINT_DESTINATION(INST) & 0x2) >> 1)
#define FLOATING_POINT_TRUE_FALSE(INST) (FLOATING_POINT_DESTINATION(INST) & 0x1)

#define TARGET(INST) (INST)->fields.target
#define SET_TARGET(INST, VAL) (INST)->fields.target = (memory_address)(VAL)

#define ENCODING(INST) (INST)->encoding
#define SET_ENCODING(INST, VAL) (INST)->encoding = (int32_t)(VAL)

#define EXPRESSION(INST) (INST)->expression
#define SET_EXPRESSION(INST, VAL) \
  (INST)->expression = (immediate_expression*)(VAL)

#define SOURCE(INST) (INST)->source_line
#define SET_SOURCE(INST, VAL) (INST)->source_line = (char*)(VAL)

/* FP-compare condition flag bits, OR'd together to build the
   condition field of c.cond.fmt instructions. */
constexpr uint32_t CONDITION_UNORDERED = 0x1;
constexpr uint32_t CONDITION_EQUAL = 0x2;
constexpr uint32_t CONDITION_LESS_THAN = 0x4;
constexpr uint32_t CONDITION_INVALID = 0x8;

/* Minimum and maximum values that fit in instruction's imm field
   (signed 16-bit and unsigned 16-bit respectively). */
constexpr int32_t IMMEDIATE_MIN = -0x8000;
constexpr int32_t IMMEDIATE_MAX = 0x7fff;

constexpr uint32_t UNSIGNED_IMMEDIATE_MIN = 0;
constexpr uint32_t UNSIGNED_IMMEDIATE_MAX = 0xffff;

/* Raise an exception! */

extern bool exception_occurred;
extern int first_bad_exception;

/* The MISC arg is injected as-is into the macro body and is expected to
   transfer control out of the enclosing context — most commonly `break`
   (terminate the switch case in run_spim) or `return true` (early-return
   from run_spim).  That requires the macro to expand to a bare brace
   block: `do { ... } while (0)` would scope the `break` to the do-while
   itself, which would silently leave the switch case running.  The
   trailing-semicolon hazard in if/else chains is mitigated by the
   convention that every call site puts its own `;` at the end. */
#define RAISE_EXCEPTION(EXCODE, MISC) \
  {                                   \
    raise_exception(EXCODE);          \
    MISC;                             \
  }

#define RAISE_INTERRUPT(LEVEL)                      \
  do {                                              \
    /* Set IP (pending) bit for interrupt level. */ \
    coprocessor_0_cause |= (1 << ((LEVEL) + 8));    \
  } while (0)

#define CLEAR_INTERRUPT(LEVEL)                        \
  do {                                                \
    /* Clear IP (pending) bit for interrupt level. */ \
    coprocessor_0_cause &= ~(1 << ((LEVEL) + 8));     \
  } while (0)

/* Recognized exceptions.  Values from the MIPS32 Cause register's
   ExcCode field, sparse 0..30.  uint8_t-backed since values fit. */
typedef enum mips_exception_code : uint8_t {
  EXCEPTION_CODE_INTERRUPT = 0, /* Interrupt */
  EXCEPTION_CODE_TRANSLATION_LOOKASIDE_BUFFER_MODIFIED =
      1, /* TLB modification (not implemented) */
  EXCEPTION_CODE_TRANSLATION_LOOKASIDE_BUFFER_LOAD =
      2, /* TLB exception (not implemented) */
  EXCEPTION_CODE_TRANSLATION_LOOKASIDE_BUFFER_STORE =
      3,                                  /* TLB exception (not implemented) */
  EXCEPTION_CODE_ADDRESS_ERROR_LOAD = 4,  /* Address error (load/fetch) */
  EXCEPTION_CODE_ADDRESS_ERROR_STORE = 5, /* Address error (store) */
  EXCEPTION_CODE_INSTRUCTION_BUS_ERROR = 6, /* Bus error, instruction fetch */
  EXCEPTION_CODE_DATA_BUS_ERROR = 7,        /* Bus error, data reference */
  EXCEPTION_CODE_SYSTEM_CALL = 8,           /* Syscall exception */
  EXCEPTION_CODE_BREAKPOINT = 9,            /* Breakpoint exception */
  EXCEPTION_CODE_RESERVED_INSTRUCTION = 10, /* Reserve instruction */
  EXCEPTION_CODE_COPROCESSOR_UNUSABLE = 11, /* Coprocessor unusable */
  EXCEPTION_CODE_ARITHMETIC_OVERFLOW = 12,  /* Arithmetic overflow */
  EXCEPTION_CODE_TRAP = 13,                 /* Trap */
  EXCEPTION_CODE_FLOATING_POINT_EXCEPTION = 15, /* Floating point */
  EXCEPTION_CODE_COPROCESSOR_2_EXCEPTION =
      18,                            /* Coprocessor 2 (not implemented) */
  EXCEPTION_CODE_MDMX = 22,          /* MDMX unusable (not implemented) */
  EXCEPTION_CODE_WATCH = 23,         /* Reference to Watch (not implemented) */
  EXCEPTION_CODE_MACHINE_CHECK = 24, /* Machine check (not implemented) */
  EXCEPTION_CODE_CACHE_ERROR = 30,   /* Cache error (not implemented) */
} mips_exception_code;

/* Fields in binary representation of instructions: */

#define BINARY_REGISTER(V, O) (((V) >> O) & 0x1f)
#define BINARY_SOURCE_REGISTER(V) (BINARY_REGISTER(V, 21))
#define BINARY_TARGET_REGISTER(V) (BINARY_REGISTER(V, 16))
#define BINARY_DESTINATION_REGISTER(V) (BINARY_REGISTER(V, 11))
#define BINARY_SHIFT_AMOUNT(V) (BINARY_REGISTER(V, 6))

#define BINARY_BASE(V) (BINARY_REGISTER(V, 21))
#define BINARY_FLOATING_POINT_TARGET(V) (BINARY_REGISTER(V, 16))
#define BINARY_FLOATING_POINT_SOURCE(V) (BINARY_REGISTER(V, 11))
#define BINARY_FLOATING_POINT_DESTINATION(V) (BINARY_REGISTER(V, 6))

/* Exported functions: */

immediate_expression* address_expression_immediate(
    const address_expression* expression);
int address_expression_register(const address_expression* expression);
void align_text(int alignment);
[[nodiscard]] immediate_expression* constant_immediate_expression(
    int32_t value);
[[nodiscard]] mips_instruction* copy_instruction(
    const mips_instruction* instruction);
memory_address current_text_location(void);
int32_t evaluate_immediate_expression(const immediate_expression* expression);
void format_an_instruction(string_stream* stream, mips_instruction* instruction,
                           memory_address address);
void free_instruction(mips_instruction* instruction);
/* Override the source-line text store_instruction records, for deferred
   (AST) emission where the scanner has moved past the instruction's
   line.  emit_ast sets it per node (may be null) and clears it when
   done; while cleared, store_instruction reads the live scanner line. */
/* Bracket the emission of one source instruction (MNEMONIC is the
   mnemonic written; IS_PSEUDO says it is a pseudo-op).  The instructions
   stored in between get their expansion_* fields set at
   expansion_end: always for a pseudo-op, and for a real instruction only
   when it became more than one.  A begin inside another begin is part of
   the outer one. */
void expansion_begin(const char* mnemonic, bool is_pseudo);
void expansion_end(void);
void emit_source_set(const char* s);
void emit_source_clear(void);
void i_type(int opcode, int target_register, int source_register,
            const immediate_expression* expression);
void i_type_free(int opcode, int target_register, int source_register,
                 immediate_expression* expression);
[[nodiscard]] immediate_expression* increment_expression_offset(
    const immediate_expression* expression, int32_t value);
[[nodiscard]] mips_instruction* instruction_decode(int32_t value);
int32_t instruction_encode(const mips_instruction* instruction);
bool instruction_is_breakpoint(memory_address address);
[[nodiscard]] const char* instruction_opcode_name(
    const mips_instruction* instruction);
/* Lookup mnemonic for a raw opcode token (TOKEN_*_OPCODE, TOKEN_*_PSEUDO_OP).
   Returned pointer is owned by the static opcode_table (opcode-tables.h). */
[[nodiscard]] const char* opcode_token_name(int opcode_token);
void j_type(int opcode, immediate_expression* target);
void kernel_text_begins_at_point(memory_address address);
[[nodiscard]] address_expression* make_address_expression(int offset,
                                                          const char* symbol,
                                                          int register_no);
[[nodiscard]] immediate_expression* make_immediate_expression(
    int offset, const char* symbol, bool is_program_counter_relative);
bool opcode_is_branch(int opcode);
bool opcode_is_nullified_branch(int opcode);
bool opcode_is_true_branch(int opcode);
bool opcode_is_jump(int opcode);
bool opcode_is_load_store(int opcode);
void print_instruction(memory_address address);
[[nodiscard]] char* instruction_to_string(memory_address address);
void r_coprocessor_type(int opcode, int floating_point_destination,
                        int floating_point_source, int floating_point_target);
void r_condition_type(int opcode, int floating_point_source,
                      int floating_point_target, int condition_code);
void r_shift_amount_type(int opcode, int destination_register,
                         int target_register, int shift_amount);
void r_type(int opcode, int destination_register, int source_register,
            int target_register);
void raise_exception(mips_exception_code exception_code);
[[nodiscard]] mips_instruction* set_breakpoint(memory_address address);
void test_assembly(mips_instruction* instruction);
void text_begins_at_point(memory_address address);
void user_kernel_text_segment(bool to_kernel);
bool is_zero_immediate(const immediate_expression* expression);

#endif
