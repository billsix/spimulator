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

typedef struct immexpr {
  int offset;         /* Offset from symbol */
  struct lab* symbol; /* Symbolic label */
  short bits;         /* > 0 => 31..16, < 0 => 15..0 */
  bool pc_relative;   /* => offset from label in code */
} imm_expr;

/* Representation of the expression that produce an address for an
   instruction.  Address have the form: label +/- offset (register). */

typedef struct addrexpr {
  unsigned char reg_no; /* Register number */
  imm_expr* imm;        /* The immediate part */
} addr_expr;

/* Representation of an instruction. Store the instruction fields in an
   overlapping manner similar to the real encoding (but not identical, to
   speed decoding in C code, as opposed to hardware).. */

typedef struct inst_s {
  short opcode;

  union {
    /* R-type or I-type: */
    struct {
      unsigned char rs;
      unsigned char rt;

      union {
        short imm;

        struct {
          unsigned char rd;
          unsigned char shamt;
        } r;
      } r_i;
    } r_i;

    /* J-type: */
    mem_addr target;
  } r_t;

  int32_t encoding;
  imm_expr* expr;
  char* source_line;
} instruction;

#define OPCODE(INST) (INST)->opcode
#define SET_OPCODE(INST, VAL) (INST)->opcode = (short)(VAL)

#define RS(INST) (INST)->r_t.r_i.rs
#define SET_RS(INST, VAL) (INST)->r_t.r_i.rs = (unsigned char)(VAL)

#define RT(INST) (INST)->r_t.r_i.rt
#define SET_RT(INST, VAL) (INST)->r_t.r_i.rt = (unsigned char)(VAL)

#define RD(INST) (INST)->r_t.r_i.r_i.r.rd
#define SET_RD(INST, VAL) (INST)->r_t.r_i.r_i.r.rd = (unsigned char)(VAL)

#define FS(INST) RD(INST)
#define SET_FS(INST, VAL) SET_RD(INST, VAL)

#define FT(INST) RT(INST)
#define SET_FT(INST, VAL) SET_RT(INST, VAL)

#define FD(INST) SHAMT(INST)
#define SET_FD(INST, VAL) SET_SHAMT(INST, VAL)

#define SHAMT(INST) (INST)->r_t.r_i.r_i.r.shamt
#define SET_SHAMT(INST, VAL) (INST)->r_t.r_i.r_i.r.shamt = (unsigned char)(VAL)

#define IMM(INST) (INST)->r_t.r_i.r_i.imm
#define SET_IMM(INST, VAL) (INST)->r_t.r_i.r_i.imm = (short)(VAL)

#define BASE(INST) RS(INST)
#define SET_BASE(INST, VAL) SET_RS(INST, VAL)

#define IOFFSET(INST) IMM(INST)
#define SET_IOFFSET(INST, VAL) SET_IMM(INST, VAL)
#define BRANCH_OFFSET(INST) (sign_ex(IOFFSET(INST) << 2))

#define COND(INST) RS(INST)
#define SET_COND(INST, VAL) SET_RS(INST, VAL)

#define CC(INST) (RT(INST) >> 2)
#define ND(INST) ((RT(INST) & 0x2) >> 1)
#define TF(INST) (RT(INST) & 0x1)

#define CCFP(INST) (FD(INST) >> 2)
#define NDFP(INST) ((FD(INST) & 0x2) >> 1)
#define TFF(INST) (FD(INST) & 0x1)

#define TARGET(INST) (INST)->r_t.target
#define SET_TARGET(INST, VAL) (INST)->r_t.target = (mem_addr)(VAL)

#define ENCODING(INST) (INST)->encoding
#define SET_ENCODING(INST, VAL) (INST)->encoding = (int32_t)(VAL)

#define EXPR(INST) (INST)->expr
#define SET_EXPR(INST, VAL) (INST)->expr = (imm_expr*)(VAL)

#define SOURCE(INST) (INST)->source_line
#define SET_SOURCE(INST, VAL) (INST)->source_line = (char*)(VAL)

/* FP-compare condition flag bits, OR'd together to build the
   condition field of c.cond.fmt instructions. */
constexpr uint32_t COND_UN = 0x1;
constexpr uint32_t COND_EQ = 0x2;
constexpr uint32_t COND_LT = 0x4;
constexpr uint32_t COND_IN = 0x8;

/* Minimum and maximum values that fit in instruction's imm field
   (signed 16-bit and unsigned 16-bit respectively). */
constexpr int32_t IMM_MIN = -0x8000;
constexpr int32_t IMM_MAX = 0x7fff;

constexpr uint32_t UIMM_MIN = 0;
constexpr uint32_t UIMM_MAX = 0xffff;

/* Raise an exception! */

extern int exception_occurred;
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

#define RAISE_INTERRUPT(LEVEL)                        \
  do {                                                \
    /* Set IP (pending) bit for interrupt level. */   \
    CP0_Cause |= (1 << ((LEVEL) + 8));                \
  } while (0)

#define CLEAR_INTERRUPT(LEVEL)                        \
  do {                                                \
    /* Clear IP (pending) bit for interrupt level. */ \
    CP0_Cause &= ~(1 << ((LEVEL) + 8));               \
  } while (0)

/* Recognized exceptions.  Values from the MIPS32 Cause register's
   ExcCode field, sparse 0..30.  uint8_t-backed since values fit. */
typedef enum mips_exc_code : uint8_t {
  ExcCode_Int = 0,       /* Interrupt */
  ExcCode_Mod = 1,       /* TLB modification (not implemented) */
  ExcCode_TLBL = 2,      /* TLB exception (not implemented) */
  ExcCode_TLBS = 3,      /* TLB exception (not implemented) */
  ExcCode_AdEL = 4,      /* Address error (load/fetch) */
  ExcCode_AdES = 5,      /* Address error (store) */
  ExcCode_IBE = 6,       /* Bus error, instruction fetch */
  ExcCode_DBE = 7,       /* Bus error, data reference */
  ExcCode_Sys = 8,       /* Syscall exception */
  ExcCode_Bp = 9,        /* Breakpoint exception */
  ExcCode_RI = 10,       /* Reserve instruction */
  ExcCode_CpU = 11,      /* Coprocessor unusable */
  ExcCode_Ov = 12,       /* Arithmetic overflow */
  ExcCode_Tr = 13,       /* Trap */
  ExcCode_FPE = 15,      /* Floating point */
  ExcCode_C2E = 18,      /* Coprocessor 2 (not implemented) */
  ExcCode_MDMX = 22,     /* MDMX unusable (not implemented) */
  ExcCode_WATCH = 23,    /* Reference to Watch (not implemented) */
  ExcCode_MCheck = 24,   /* Machine check (not implemented) */
  ExcCode_CacheErr = 30, /* Cache error (not implemented) */
} mips_exc_code;

/* Fields in binary representation of instructions: */

#define BIN_REG(V, O) (((V) >> O) & 0x1f)
#define BIN_RS(V) (BIN_REG(V, 21))
#define BIN_RT(V) (BIN_REG(V, 16))
#define BIN_RD(V) (BIN_REG(V, 11))
#define BIN_SHAMT(V) (BIN_REG(V, 6))

#define BIN_BASE(V) (BIN_REG(V, 21))
#define BIN_FT(V) (BIN_REG(V, 16))
#define BIN_FS(V) (BIN_REG(V, 11))
#define BIN_FD(V) (BIN_REG(V, 6))

/* Exported functions: */

imm_expr* addr_expr_imm(addr_expr* expr);
int addr_expr_reg(addr_expr* expr);
void align_text(int alignment);
[[nodiscard]] imm_expr* const_imm_expr(int32_t value);
[[nodiscard]] instruction* copy_inst(instruction* inst);
mem_addr current_text_pc(void);
int32_t eval_imm_expr(imm_expr* expr);
void format_an_inst(str_stream* ss, instruction* inst, mem_addr addr);
void free_inst(instruction* inst);
void i_type_inst(int opcode, int rt, int rs, imm_expr* expr);
void i_type_inst_free(int opcode, int rt, int rs, imm_expr* expr);
[[nodiscard]] imm_expr* incr_expr_offset(imm_expr* expr, int32_t value);
void initialize_inst_tables(void);
[[nodiscard]] instruction* inst_decode(int32_t value);
int32_t inst_encode(instruction* inst);
bool inst_is_breakpoint(mem_addr addr);
const char* inst_op_name(instruction* inst);
/* Lookup mnemonic for a raw opcode token (TOK_*_OP, TOK_*_POP).
   Returned pointer is owned by inst.c's static name_tbl. */
const char* op_token_name(int op_token);
void j_type_inst(int opcode, imm_expr* target);
void k_text_begins_at_point(mem_addr addr);
[[nodiscard]] addr_expr* make_addr_expr(int offs, char* sym, int reg_no);
[[nodiscard]] imm_expr* make_imm_expr(int offs, char* sym, bool is_pc_relative);
bool opcode_is_branch(int opcode);
bool opcode_is_nullified_branch(int opcode);
bool opcode_is_true_branch(int opcode);
bool opcode_is_jump(int opcode);
bool opcode_is_load_store(int opcode);
void print_inst(mem_addr addr);
[[nodiscard]] char* inst_to_string(mem_addr addr);
void r_co_type_inst(int opcode, int fd, int fs, int ft);
void r_cond_type_inst(int opcode, int fs, int ft, int cc);
void r_sh_type_inst(int opcode, int rd, int rt, int shamt);
void r_type_inst(int opcode, int rd, int rs, int rt);
void raise_exception(mips_exc_code excode);
[[nodiscard]] instruction* set_breakpoint(mem_addr addr);
void test_assembly(instruction* inst);
void text_begins_at_point(mem_addr addr);
void user_kernel_text_segment(bool to_kernel);
bool is_zero_imm(imm_expr* expr);

#endif
