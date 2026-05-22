/* SPIM S20 MIPS simulator.
   Execute SPIM instructions.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#ifdef mips
#define _IEEE 1
#include <nan.h>
#else
#define NaN(X) ((X) != (X))
#endif

#include <math.h>
#include <stdio.h>

#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>

#include "spim.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "inst.h"
#include "reg.h"
#include "mem.h"
#include "sym-tbl.h"
#include "tokens.h"
#include "syscall.h"
#include "run.h"
#include "explain.h"

bool force_break =
    false; /* For the execution env. to force an execution break */

/* Local functions: */

static void bump_CP0_timer(void);
static void set_fpu_cc(int cond, int cc, int less, int equal, int unordered);
static void signed_multiply(reg_word v1, reg_word v2);
static void start_CP0_timer(void);
static void unsigned_multiply(reg_word v1, reg_word v2);

#define SIGN_BIT(X) ((X) & 0x80000000)

#define ARITH_OVFL(RESULT, OP1, OP2) \
  (SIGN_BIT(OP1) == SIGN_BIT(OP2) && SIGN_BIT(OP1) != SIGN_BIT(RESULT))

/* True when delayed_branches is true and instruction is executing in delay
slot of another instruction. */
static int running_in_delay_slot = 0;

/* Executed delayed branch and jump instructions by running the
   instruction from the delay slot before transfering control.  Note,
   in branches that don't jump, the instruction in the delay slot is
   executed by falling through normally.

   We take advantage of the MIPS architecture, which leaves undefined
   the result of executing a delayed instruction in a delay slot.  Here
   we execute the second branch. */

#define BRANCH_INST(TEST, TARGET, NULLIFY)       \
  {                                              \
    if (TEST) {                                  \
      mem_addr target = (TARGET);                \
      if (delayed_branches) {                    \
        /* +4 since jump in delay slot */        \
        target += BYTES_PER_WORD;                \
      }                                          \
      JUMP_INST(target)                          \
    } else if (NULLIFY) {                        \
      /* If test fails and nullify bit set, skip \
         instruction in delay slot. */           \
      PC += BYTES_PER_WORD;                      \
    }                                            \
  }

#define JUMP_INST(TARGET)                        \
  {                                              \
    if (delayed_branches) {                      \
      running_in_delay_slot = 1;                 \
      run_spim(PC + BYTES_PER_WORD, 1, display); \
      running_in_delay_slot = 0;                 \
    }                                            \
    /* -4 since PC is bumped after this inst */  \
    PC = (TARGET) - BYTES_PER_WORD;              \
  }

/* If the delayed_load flag is false, the result from a load is available
   immediate.  If the delayed_load flag is true, the result from a load is
   not available until the subsequent instruction has executed (as in the
   real machine). We need a two element shift register for the value and its
   destination, as the instruction following the load can itself be a load
   instruction. */

#define LOAD_INST(DEST_A, LD, MASK) {LOAD_INST_BASE(DEST_A, (LD & (MASK)))}

#define LOAD_INST_BASE(DEST_A, VALUE) \
  {                                   \
    if (delayed_loads) {              \
      delayed_load_addr1 = (DEST_A);  \
      delayed_load_value1 = (VALUE);  \
    } else {                          \
      *(DEST_A) = (VALUE);            \
    }                                 \
  }

#define DO_DELAYED_UPDATE()                      \
  if (delayed_loads) {                           \
    /* Check for delayed updates */              \
    if (delayed_load_addr2 != nullptr) {         \
      *delayed_load_addr2 = delayed_load_value2; \
    }                                            \
    delayed_load_addr2 = delayed_load_addr1;     \
    delayed_load_value2 = delayed_load_value1;   \
    delayed_load_addr1 = nullptr;                \
  }

/* Run the program stored in memory, starting at address PC for
   STEPS_TO_RUN instruction executions.  If flag DISPLAY is true, print
   each instruction before it executes. Return true if program's
   execution can continue. */

bool run_spim(mem_addr initial_PC, int steps_to_run, bool display) {
  instruction* inst;
  static reg_word *delayed_load_addr1 = nullptr, delayed_load_value1;
  static reg_word *delayed_load_addr2 = nullptr, delayed_load_value2;
  int step, step_size, next_step;

  PC = initial_PC;
  if (!bare_machine && mapped_io)
    next_step = IO_INTERVAL;
  else
    next_step = steps_to_run; /* Run to completion */

  /* Start a timer running */
  start_CP0_timer();

  for (step_size = MIN(next_step, steps_to_run); steps_to_run > 0;
       steps_to_run -= step_size, step_size = MIN(next_step, steps_to_run)) {
    if (!bare_machine && mapped_io)
      /* Every IO_INTERVAL steps, check if memory-mapped IO registers
         have changed. */
      check_memory_mapped_IO();
    /* else run inner loop for all steps */

    if ((CP0_Status & CP0_Status_IE) && !(CP0_Status & CP0_Status_EXL) &&
        ((CP0_Cause & CP0_Cause_IP) & (CP0_Status & CP0_Status_IM))) {
      /* There is an interrupt to process if IE bit set, EXL bit not
         set, and non-masked IP bit set */
      raise_exception(ExcCode_Int);
      /* Handle interrupt now, before instruction executes, so that
         EPC points to unexecuted instructions, which is the one to
         return to. */
      handle_exception();
    }

    force_break = false;
    for (step = 0; step < step_size; step += 1) {
      if (force_break) {
        return true;
      }

      R[0] = 0; /* Maintain invariant value */

      {
        /* Poll for timer expiration */
        struct itimerval time;
        if (-1 == getitimer(ITIMER_REAL, &time)) {
          perror("getitmer failed");
        }
        if (time.it_value.tv_usec == 0 && time.it_value.tv_sec == 0) {
          /* Timer expired */
          bump_CP0_timer();

          /* Restart timer for next interval */
          start_CP0_timer();
        }
      }

      exception_occurred = 0;
      inst = mem_read_inst(PC);
      if (exception_occurred) /* In reading instruction */
      {
        exception_occurred = 0;
        handle_exception();
        continue;
      } else if (inst == nullptr) {
        run_error("Attempt to execute non-instruction at 0x%08x\n", PC);
        return false;
      } else if (EXPR(inst) != nullptr && EXPR(inst)->symbol != nullptr &&
                 EXPR(inst)->symbol->addr == 0) {
        run_error("Instruction references undefined symbol at 0x%08x\n  %s", PC,
                  inst_to_string(PC));
        return false;
      }

#ifdef TEST_ASM
      test_assembly(inst);
#endif

      DO_DELAYED_UPDATE();

      explain_before(inst, PC);

      switch (OPCODE(inst)) {
        case TOK_ADD_OP: {
          reg_word vs = R[RS(inst)], vt = R[RT(inst)];
          reg_word sum = vs + vt;

          if (ARITH_OVFL(sum, vs, vt)) RAISE_EXCEPTION(ExcCode_Ov, break);
          R[RD(inst)] = sum;
          break;
        }

        case TOK_ADDI_OP: {
          reg_word vs = R[RS(inst)], imm = (short)IMM(inst);
          reg_word sum = vs + imm;

          if (ARITH_OVFL(sum, vs, imm)) RAISE_EXCEPTION(ExcCode_Ov, break);
          R[RT(inst)] = sum;
          break;
        }

        case TOK_ADDIU_OP:
          R[RT(inst)] = R[RS(inst)] + (short)IMM(inst);
          break;

        case TOK_ADDU_OP:
          R[RD(inst)] = R[RS(inst)] + R[RT(inst)];
          break;

        case TOK_AND_OP:
          R[RD(inst)] = R[RS(inst)] & R[RT(inst)];
          break;

        case TOK_ANDI_OP:
          R[RT(inst)] = R[RS(inst)] & (0xffff & IMM(inst));
          break;

        case TOK_BC2F_OP:
        case TOK_BC2FL_OP:
        case TOK_BC2T_OP:
        case TOK_BC2TL_OP:
          RAISE_EXCEPTION(ExcCode_CpU, {}); /* No Coprocessor 2 */
          break;

        case TOK_BEQ_OP:
          BRANCH_INST(R[RS(inst)] == R[RT(inst)], PC + IDISP(inst), 0);
          break;

        case TOK_BEQL_OP:
          BRANCH_INST(R[RS(inst)] == R[RT(inst)], PC + IDISP(inst), 1);
          break;

        case TOK_BGEZ_OP:
          BRANCH_INST(SIGN_BIT(R[RS(inst)]) == 0, PC + IDISP(inst), 0);
          break;

        case TOK_BGEZL_OP:
          BRANCH_INST(SIGN_BIT(R[RS(inst)]) == 0, PC + IDISP(inst), 1);
          break;

        case TOK_BGEZAL_OP:
          R[31] = PC + (delayed_branches ? 2 * BYTES_PER_WORD : BYTES_PER_WORD);
          BRANCH_INST(SIGN_BIT(R[RS(inst)]) == 0, PC + IDISP(inst), 0);
          break;

        case TOK_BGEZALL_OP:
          R[31] = PC + (delayed_branches ? 2 * BYTES_PER_WORD : BYTES_PER_WORD);
          BRANCH_INST(SIGN_BIT(R[RS(inst)]) == 0, PC + IDISP(inst), 1);
          break;

        case TOK_BGTZ_OP:
          BRANCH_INST(R[RS(inst)] != 0 && SIGN_BIT(R[RS(inst)]) == 0,
                      PC + IDISP(inst), 0);
          break;

        case TOK_BGTZL_OP:
          BRANCH_INST(R[RS(inst)] != 0 && SIGN_BIT(R[RS(inst)]) == 0,
                      PC + IDISP(inst), 1);
          break;

        case TOK_BLEZ_OP:
          BRANCH_INST(R[RS(inst)] == 0 || SIGN_BIT(R[RS(inst)]) != 0,
                      PC + IDISP(inst), 0);
          break;

        case TOK_BLEZL_OP:
          BRANCH_INST(R[RS(inst)] == 0 || SIGN_BIT(R[RS(inst)]) != 0,
                      PC + IDISP(inst), 1);
          break;

        case TOK_BLTZ_OP:
          BRANCH_INST(SIGN_BIT(R[RS(inst)]) != 0, PC + IDISP(inst), 0);
          break;

        case TOK_BLTZL_OP:
          BRANCH_INST(SIGN_BIT(R[RS(inst)]) != 0, PC + IDISP(inst), 1);
          break;

        case TOK_BLTZAL_OP:
          R[31] = PC + (delayed_branches ? 2 * BYTES_PER_WORD : BYTES_PER_WORD);
          BRANCH_INST(SIGN_BIT(R[RS(inst)]) != 0, PC + IDISP(inst), 0);
          break;

        case TOK_BLTZALL_OP:
          R[31] = PC + (delayed_branches ? 2 * BYTES_PER_WORD : BYTES_PER_WORD);
          BRANCH_INST(SIGN_BIT(R[RS(inst)]) != 0, PC + IDISP(inst), 1);
          break;

        case TOK_BNE_OP:
          BRANCH_INST(R[RS(inst)] != R[RT(inst)], PC + IDISP(inst), 0);
          break;

        case TOK_BNEL_OP:
          BRANCH_INST(R[RS(inst)] != R[RT(inst)], PC + IDISP(inst), 1);
          break;

        case TOK_BREAK_OP:
          if (RD(inst) == 1) /* Debugger breakpoint */
            RAISE_EXCEPTION(ExcCode_Bp, return true)
          else
            RAISE_EXCEPTION(ExcCode_Bp, break);

        case TOK_CACHE_OP:
          break; /* Memory details not implemented */

        case TOK_CFC0_OP:
          R[RT(inst)] = CCR[0][RD(inst)];
          break;

        case TOK_CFC2_OP:
          RAISE_EXCEPTION(ExcCode_CpU, {}); /* No Coprocessor 2 */
          break;

        case TOK_CLO_OP: {
          reg_word val = R[RS(inst)];
          int i;
          for (i = 31; 0 <= i; i -= 1)
            if (((val >> i) & 0x1) == 0) break;

          R[RD(inst)] = 31 - i;
          break;
        }

        case TOK_CLZ_OP: {
          reg_word val = R[RS(inst)];
          int i;
          for (i = 31; 0 <= i; i -= 1)
            if (((val >> i) & 0x1) == 1) break;

          R[RD(inst)] = 31 - i;
          break;
        }

        case TOK_COP2_OP:
          RAISE_EXCEPTION(ExcCode_CpU, {}); /* No Coprocessor 2 */
          break;

        case TOK_CTC0_OP:
          CCR[0][RD(inst)] = R[RT(inst)];
          break;

        case TOK_CTC2_OP:
          RAISE_EXCEPTION(ExcCode_CpU, {}); /* No Coprocessor 2 */
          break;

        case TOK_DIV_OP:
          /* The behavior of this instruction is undefined on divide by
             zero or overflow. */
          if (R[RT(inst)] != 0 && !(R[RS(inst)] == (reg_word)0x80000000 &&
                                    R[RT(inst)] == (reg_word)0xffffffff)) {
            LO = (reg_word)R[RS(inst)] / (reg_word)R[RT(inst)];
            HI = (reg_word)R[RS(inst)] % (reg_word)R[RT(inst)];
          }
          break;

        case TOK_DIVU_OP:
          /* The behavior of this instruction is undefined on divide by
             zero or overflow. */
          if (R[RT(inst)] != 0 && !(R[RS(inst)] == (reg_word)0x80000000 &&
                                    R[RT(inst)] == (reg_word)0xffffffff)) {
            LO = (u_reg_word)R[RS(inst)] / (u_reg_word)R[RT(inst)];
            HI = (u_reg_word)R[RS(inst)] % (u_reg_word)R[RT(inst)];
          }
          break;

        case TOK_ERET_OP: {
          CP0_Status &= ~CP0_Status_EXL; /* Clear EXL bit */
          JUMP_INST(CP0_EPC);            /* Jump to EPC */
        } break;

        case TOK_J_OP:
          JUMP_INST(((PC & 0xf0000000) | TARGET(inst) << 2));
          break;

        case TOK_JAL_OP:
          if (delayed_branches)
            R[31] = PC + 2 * BYTES_PER_WORD;
          else
            R[31] = PC + BYTES_PER_WORD;
          JUMP_INST(((PC & 0xf0000000) | (TARGET(inst) << 2)));
          break;

        case TOK_JALR_OP: {
          mem_addr tmp = R[RS(inst)];

          if (delayed_branches)
            R[RD(inst)] = PC + 2 * BYTES_PER_WORD;
          else
            R[RD(inst)] = PC + BYTES_PER_WORD;
          JUMP_INST(tmp);
        } break;

        case TOK_JR_OP: {
          mem_addr tmp = R[RS(inst)];

          JUMP_INST(tmp);
        } break;

        case TOK_LB_OP:
          LOAD_INST(&R[RT(inst)], mem_read_byte(R[BASE(inst)] + IOFFSET(inst)),
                    0xffffffff);
          break;

        case TOK_LBU_OP:
          LOAD_INST(&R[RT(inst)], mem_read_byte(R[BASE(inst)] + IOFFSET(inst)),
                    0xff);
          break;

        case TOK_LH_OP:
          LOAD_INST(&R[RT(inst)], mem_read_half(R[BASE(inst)] + IOFFSET(inst)),
                    0xffffffff);
          break;

        case TOK_LHU_OP:
          LOAD_INST(&R[RT(inst)], mem_read_half(R[BASE(inst)] + IOFFSET(inst)),
                    0xffff);
          break;

        case TOK_LL_OP:
          /* Uniprocess, so this instruction is just a load */
          LOAD_INST(&R[RT(inst)], mem_read_word(R[BASE(inst)] + IOFFSET(inst)),
                    0xffffffff);
          break;

        case TOK_LUI_OP:
          R[RT(inst)] = (IMM(inst) << 16) & 0xffff0000;
          break;

        case TOK_LW_OP:
          LOAD_INST(&R[RT(inst)], mem_read_word(R[BASE(inst)] + IOFFSET(inst)),
                    0xffffffff);
          break;

        case TOK_LDC2_OP:
          RAISE_EXCEPTION(ExcCode_CpU, {}); /* No Coprocessor 2 */
          break;

        case TOK_LWC2_OP:
          RAISE_EXCEPTION(ExcCode_CpU, {}); /* No Coprocessor 2 */
          break;

        case TOK_LWL_OP: {
          mem_addr addr = R[BASE(inst)] + IOFFSET(inst);
          reg_word word; /* Can't be register */
          int byte = addr & 0x3;
          reg_word reg_val = R[RT(inst)];

          word = mem_read_word(addr & 0xfffffffc);
          if (!exception_occurred)
#ifdef SPIM_BIGENDIAN
            switch (byte) {
              case 0:
                word = word;
                break;

              case 1:
                word = ((word & 0xffffff) << 8) | (reg_val & 0xff);
                break;

              case 2:
                word = ((word & 0xffff) << 16) | (reg_val & 0xffff);
                break;

              case 3:
                word = ((word & 0xff) << 24) | (reg_val & 0xffffff);
                break;
            }
#else
            switch (byte) {
              case 0:
                word = ((word & 0xff) << 24) | (reg_val & 0xffffff);
                break;

              case 1:
                word = ((word & 0xffff) << 16) | (reg_val & 0xffff);
                break;

              case 2:
                word = ((word & 0xffffff) << 8) | (reg_val & 0xff);
                break;

              case 3:
                break;
            }
#endif
          LOAD_INST_BASE(&R[RT(inst)], word);
          break;
        }

        case TOK_LWR_OP: {
          mem_addr addr = R[BASE(inst)] + IOFFSET(inst);
          reg_word word; /* Can't be register */
          int byte = addr & 0x3;
          reg_word reg_val = R[RT(inst)];

          word = mem_read_word(addr & 0xfffffffc);
          if (!exception_occurred)
#ifdef SPIM_BIGENDIAN
            switch (byte) {
              case 0:
                word = (reg_val & 0xffffff00) |
                       ((unsigned)(word & 0xff000000) >> 24);
                break;

              case 1:
                word = (reg_val & 0xffff0000) |
                       ((unsigned)(word & 0xffff0000) >> 16);
                break;

              case 2:
                word = (reg_val & 0xff000000) |
                       ((unsigned)(word & 0xffffff00) >> 8);
                break;

              case 3:
                word = word;
                break;
            }
#else
            switch (byte) {
              case 0:
                break;

              case 1:
                word = (reg_val & 0xff000000) | ((word & 0xffffff00) >> 8);
                break;

              case 2:
                word = (reg_val & 0xffff0000) | ((word & 0xffff0000) >> 16);
                break;

              case 3:
                word = (reg_val & 0xffffff00) | ((word & 0xff000000) >> 24);
                break;
            }
#endif
          LOAD_INST_BASE(&R[RT(inst)], word);
          break;
        }

        case TOK_MADD_OP:
        case TOK_MADDU_OP: {
          reg_word lo = LO, hi = HI;
          reg_word tmp;
          if (OPCODE(inst) == TOK_MADD_OP) {
            signed_multiply(R[RS(inst)], R[RT(inst)]);
          } else /* TOK_MADDU_OP */
          {
            unsigned_multiply(R[RS(inst)], R[RT(inst)]);
          }
          tmp = lo + LO;
          if ((unsigned)tmp < (unsigned)LO || (unsigned)tmp < (unsigned)lo) {
            /* Addition of low-order word overflows */
            hi += 1;
          }
          LO = tmp;
          HI = hi + HI;
          break;
        }

        case TOK_MFC0_OP:
          R[RT(inst)] = CPR[0][FS(inst)];
          break;

        case TOK_MFC2_OP:
          RAISE_EXCEPTION(ExcCode_CpU, {}); /* No Coprocessor 2 */
          break;

        case TOK_MFHI_OP:
          R[RD(inst)] = HI;
          break;

        case TOK_MFLO_OP:
          R[RD(inst)] = LO;
          break;

        case TOK_MOVN_OP:
          if (R[RT(inst)] != 0) R[RD(inst)] = R[RS(inst)];
          break;

        case TOK_MOVZ_OP:
          if (R[RT(inst)] == 0) R[RD(inst)] = R[RS(inst)];
          break;

        case TOK_MSUB_OP:
        case TOK_MSUBU_OP: {
          reg_word lo = LO, hi = HI;
          reg_word tmp;

          if (OPCODE(inst) == TOK_MSUB_OP) {
            signed_multiply(R[RS(inst)], R[RT(inst)]);
          } else /* TOK_MSUBU_OP */
          {
            unsigned_multiply(R[RS(inst)], R[RT(inst)]);
          }

          tmp = lo - LO;
          if ((unsigned)LO > (unsigned)lo) {
            /* Subtraction of low-order word borrows */
            hi -= 1;
          }
          LO = tmp;
          HI = hi - HI;
          break;
        }

        case TOK_MTC0_OP:
          CPR[0][FS(inst)] = R[RT(inst)];
          switch (FS(inst)) {
            case CP0_Compare_Reg:
              CP0_Cause &= ~CP0_Cause_IP7; /* Writing clears HW interrupt 5 */
              break;

            case CP0_Status_Reg:
              CP0_Status &= CP0_Status_Mask;
              CP0_Status |= ((CP0_Status_CU & 0x30000000) | CP0_Status_UM);
              break;

            case CP0_Cause_Reg:
              CPR[0][FS(inst)] &= CP0_Cause_Mask;
              break;

            case CP0_Config_Reg:
              CPR[0][FS(inst)] &= CP0_Config_Mask;
              break;

            default:
              break;
          }
          break;

        case TOK_MTC2_OP:
          RAISE_EXCEPTION(ExcCode_CpU, {}); /* No Coprocessor 2 */
          break;

        case TOK_MTHI_OP:
          HI = R[RS(inst)];
          break;

        case TOK_MTLO_OP:
          LO = R[RS(inst)];
          break;

        case TOK_MUL_OP:
          signed_multiply(R[RS(inst)], R[RT(inst)]);
          R[RD(inst)] = LO;
          break;

        case TOK_MULT_OP:
          signed_multiply(R[RS(inst)], R[RT(inst)]);
          break;

        case TOK_MULTU_OP:
          unsigned_multiply(R[RS(inst)], R[RT(inst)]);
          break;

        case TOK_NOR_OP:
          R[RD(inst)] = ~(R[RS(inst)] | R[RT(inst)]);
          break;

        case TOK_OR_OP:
          R[RD(inst)] = R[RS(inst)] | R[RT(inst)];
          break;

        case TOK_ORI_OP:
          R[RT(inst)] = R[RS(inst)] | (0xffff & IMM(inst));
          break;

        case TOK_PREF_OP:
          break; /* Memory details not implemented */

        case TOK_RFE_OP:
#ifdef MIPS1
          /* This is MIPS-I, not compatible with MIPS32 or the
             definition of the bits in the CP0 Status register in that
             architecture. */
          CP0_Status = (CP0_Status & 0xfffffff0) | ((CP0_Status & 0x3c) >> 2);
#else
          RAISE_EXCEPTION(ExcCode_RI, {}); /* Not MIPS32 instruction */
#endif
          break;

        case TOK_SB_OP:
          mem_write_byte(R[BASE(inst)] + IOFFSET(inst), R[RT(inst)]);
          break;

        case TOK_SC_OP:
          /* Uniprocessor, so instruction is just a store */
          mem_write_word(R[BASE(inst)] + IOFFSET(inst), R[RT(inst)]);
          break;

        case TOK_SDC2_OP:
          RAISE_EXCEPTION(ExcCode_CpU, {}); /* No Coprocessor 2 */
          break;

        case TOK_SH_OP:
          mem_write_half(R[BASE(inst)] + IOFFSET(inst), R[RT(inst)]);
          break;

        case TOK_SLL_OP: {
          int shamt = SHAMT(inst);

          if (shamt >= 0 && shamt < 32)
            R[RD(inst)] = R[RT(inst)] << shamt;
          else
            R[RD(inst)] = R[RT(inst)];
          break;
        }

        case TOK_SLLV_OP: {
          int shamt = (R[RS(inst)] & 0x1f);

          if (shamt >= 0 && shamt < 32)
            R[RD(inst)] = R[RT(inst)] << shamt;
          else
            R[RD(inst)] = R[RT(inst)];
          break;
        }

        case TOK_SLT_OP:
          if (R[RS(inst)] < R[RT(inst)])
            R[RD(inst)] = 1;
          else
            R[RD(inst)] = 0;
          break;

        case TOK_SLTI_OP:
          if (R[RS(inst)] < (short)IMM(inst))
            R[RT(inst)] = 1;
          else
            R[RT(inst)] = 0;
          break;

        case TOK_SLTIU_OP: {
          int x = (short)IMM(inst);

          if ((u_reg_word)R[RS(inst)] < (u_reg_word)x)
            R[RT(inst)] = 1;
          else
            R[RT(inst)] = 0;
          break;
        }

        case TOK_SLTU_OP:
          if ((u_reg_word)R[RS(inst)] < (u_reg_word)R[RT(inst)])
            R[RD(inst)] = 1;
          else
            R[RD(inst)] = 0;
          break;

        case TOK_SRA_OP: {
          int shamt = SHAMT(inst);
          reg_word val = R[RT(inst)];

          if (shamt >= 0 && shamt < 32)
            R[RD(inst)] = val >> shamt;
          else
            R[RD(inst)] = val;
          break;
        }

        case TOK_SRAV_OP: {
          int shamt = R[RS(inst)] & 0x1f;
          reg_word val = R[RT(inst)];

          if (shamt >= 0 && shamt < 32)
            R[RD(inst)] = val >> shamt;
          else
            R[RD(inst)] = val;
          break;
        }

        case TOK_SRL_OP: {
          int shamt = SHAMT(inst);
          u_reg_word val = R[RT(inst)];

          if (shamt >= 0 && shamt < 32)
            R[RD(inst)] = val >> shamt;
          else
            R[RD(inst)] = val;
          break;
        }

        case TOK_SRLV_OP: {
          int shamt = R[RS(inst)] & 0x1f;
          u_reg_word val = R[RT(inst)];

          if (shamt >= 0 && shamt < 32)
            R[RD(inst)] = val >> shamt;
          else
            R[RD(inst)] = val;
          break;
        }

        case TOK_SUB_OP: {
          reg_word vs = R[RS(inst)], vt = R[RT(inst)];
          reg_word diff = vs - vt;

          if (SIGN_BIT(vs) != SIGN_BIT(vt) && SIGN_BIT(vs) != SIGN_BIT(diff))
            RAISE_EXCEPTION(ExcCode_Ov, break);
          R[RD(inst)] = diff;
          break;
        }

        case TOK_SUBU_OP:
          R[RD(inst)] = (u_reg_word)R[RS(inst)] - (u_reg_word)R[RT(inst)];
          break;

        case TOK_SW_OP:
          mem_write_word(R[BASE(inst)] + IOFFSET(inst), R[RT(inst)]);
          break;

        case TOK_SWC2_OP:
          RAISE_EXCEPTION(ExcCode_CpU, {}); /* No Coprocessor 2 */
          break;

        case TOK_SWL_OP: {
          mem_addr addr = R[BASE(inst)] + IOFFSET(inst);
          mem_word data;
          reg_word reg = R[RT(inst)];
          int byte = addr & 0x3;

          data = mem_read_word(addr & 0xfffffffc);
#ifdef SPIM_BIGENDIAN
          switch (byte) {
            case 0:
              data = reg;
              break;

            case 1:
              data = (data & 0xff000000) | (reg >> 8 & 0xffffff);
              break;

            case 2:
              data = (data & 0xffff0000) | (reg >> 16 & 0xffff);
              break;

            case 3:
              data = (data & 0xffffff00) | (reg >> 24 & 0xff);
              break;
          }
#else
          switch (byte) {
            case 0:
              data = (data & 0xffffff00) | (reg >> 24 & 0xff);
              break;

            case 1:
              data = (data & 0xffff0000) | (reg >> 16 & 0xffff);
              break;

            case 2:
              data = (data & 0xff000000) | (reg >> 8 & 0xffffff);
              break;

            case 3:
              data = reg;
              break;
          }
#endif
          mem_write_word(addr & 0xfffffffc, data);
          break;
        }

        case TOK_SWR_OP: {
          mem_addr addr = R[BASE(inst)] + IOFFSET(inst);
          mem_word data;
          reg_word reg = R[RT(inst)];
          int byte = addr & 0x3;

          data = mem_read_word(addr & 0xfffffffc);
#ifdef SPIM_BIGENDIAN
          switch (byte) {
            case 0:
              data = ((reg << 24) & 0xff000000) | (data & 0xffffff);
              break;

            case 1:
              data = ((reg << 16) & 0xffff0000) | (data & 0xffff);
              break;

            case 2:
              data = ((reg << 8) & 0xffffff00) | (data & 0xff);
              break;

            case 3:
              data = reg;
              break;
          }
#else
          switch (byte) {
            case 0:
              data = reg;
              break;

            case 1:
              data = ((reg << 8) & 0xffffff00) | (data & 0xff);
              break;

            case 2:
              data = ((reg << 16) & 0xffff0000) | (data & 0xffff);
              break;

            case 3:
              data = ((reg << 24) & 0xff000000) | (data & 0xffffff);
              break;
          }
#endif
          mem_write_word(addr & 0xfffffffc, data);
          break;
        }

        case TOK_SYNC_OP:
          break; /* Memory details not implemented */

        case TOK_SYSCALL_OP:
          if (!do_syscall()) return false;
          break;

        case TOK_TEQ_OP:
          if (R[RS(inst)] == R[RT(inst)]) RAISE_EXCEPTION(ExcCode_Tr, {});
          break;

        case TOK_TEQI_OP:
          if (R[RS(inst)] == IMM(inst)) RAISE_EXCEPTION(ExcCode_Tr, {});
          break;

        case TOK_TGE_OP:
          if (R[RS(inst)] >= R[RT(inst)]) RAISE_EXCEPTION(ExcCode_Tr, {});
          break;

        case TOK_TGEI_OP:
          if (R[RS(inst)] >= IMM(inst)) RAISE_EXCEPTION(ExcCode_Tr, {});
          break;

        case TOK_TGEIU_OP:
          if ((u_reg_word)R[RS(inst)] >= (u_reg_word)IMM(inst))
            RAISE_EXCEPTION(ExcCode_Tr, {});
          break;

        case TOK_TGEU_OP:
          if ((u_reg_word)R[RS(inst)] >= (u_reg_word)R[RT(inst)])
            RAISE_EXCEPTION(ExcCode_Tr, {});
          break;

        case TOK_TLBP_OP:
          RAISE_EXCEPTION(ExcCode_RI, {}); /* TLB not implemented */
          break;

        case TOK_TLBR_OP:
          RAISE_EXCEPTION(ExcCode_RI, {}); /* TLB not implemented */
          break;

        case TOK_TLBWI_OP:
          RAISE_EXCEPTION(ExcCode_RI, {}); /* TLB not implemented */
          break;

        case TOK_TLBWR_OP:
          RAISE_EXCEPTION(ExcCode_RI, {}); /* TLB not implemented */
          break;

        case TOK_TLT_OP:
          if (R[RS(inst)] < R[RT(inst)]) RAISE_EXCEPTION(ExcCode_Tr, {});
          break;

        case TOK_TLTI_OP:
          if (R[RS(inst)] < IMM(inst)) RAISE_EXCEPTION(ExcCode_Tr, {});
          break;

        case TOK_TLTIU_OP:
          if ((u_reg_word)R[RS(inst)] < (u_reg_word)IMM(inst))
            RAISE_EXCEPTION(ExcCode_Tr, {});
          break;

        case TOK_TLTU_OP:
          if ((u_reg_word)R[RS(inst)] < (u_reg_word)R[RT(inst)])
            RAISE_EXCEPTION(ExcCode_Tr, {});
          break;

        case TOK_TNE_OP:
          if (R[RS(inst)] != R[RT(inst)]) RAISE_EXCEPTION(ExcCode_Tr, {});
          break;

        case TOK_TNEI_OP:
          if (R[RS(inst)] != IMM(inst)) RAISE_EXCEPTION(ExcCode_Tr, {});
          break;

        case TOK_XOR_OP:
          R[RD(inst)] = R[RS(inst)] ^ R[RT(inst)];
          break;

        case TOK_XORI_OP:
          R[RT(inst)] = R[RS(inst)] ^ (0xffff & IMM(inst));
          break;

          /* FPA Operations */

        case TOK_ABS_S_OP:
          SET_FPR_S(FD(inst), fabs(FPR_S(FS(inst))));
          break;

        case TOK_ABS_D_OP:
          SET_FPR_D(FD(inst), fabs(FPR_D(FS(inst))));
          break;

        case TOK_ADD_S_OP:
          SET_FPR_S(FD(inst), FPR_S(FS(inst)) + FPR_S(FT(inst)));
          /* Should trap on inexact/overflow/underflow */
          break;

        case TOK_ADD_D_OP:
          SET_FPR_D(FD(inst), FPR_D(FS(inst)) + FPR_D(FT(inst)));
          /* Should trap on inexact/overflow/underflow */
          break;

        case TOK_BC1F_OP:
        case TOK_BC1FL_OP:
        case TOK_BC1T_OP:
        case TOK_BC1TL_OP: {
          int cc = CC(inst);
          int nd = ND(inst); /* 1 => nullify */
          int tf = TF(inst); /* 0 => BC1F, 1 => BC1T */
          BRANCH_INST(FCC(cc) == tf, PC + IDISP(inst), nd);
          break;
        }

        case TOK_C_F_S_OP:
        case TOK_C_UN_S_OP:
        case TOK_C_EQ_S_OP:
        case TOK_C_UEQ_S_OP:
        case TOK_C_OLT_S_OP:
        case TOK_C_OLE_S_OP:
        case TOK_C_ULT_S_OP:
        case TOK_C_ULE_S_OP:
        case TOK_C_SF_S_OP:
        case TOK_C_NGLE_S_OP:
        case TOK_C_SEQ_S_OP:
        case TOK_C_NGL_S_OP:
        case TOK_C_LT_S_OP:
        case TOK_C_NGE_S_OP:
        case TOK_C_LE_S_OP:
        case TOK_C_NGT_S_OP: {
          float v1 = FPR_S(FS(inst)), v2 = FPR_S(FT(inst));
          double dv1 = v1, dv2 = v2;
          int cond = COND(inst);
          int cc = CCFP(inst);

          if (NaN(dv1) || NaN(dv2)) {
            if (cond & COND_IN) {
              RAISE_EXCEPTION(ExcCode_FPE, break);
            }
            set_fpu_cc(cond, cc, 0, 0, 1);
          } else {
            set_fpu_cc(cond, cc, v1 < v2, v1 == v2, 0);
          }
        } break;

        case TOK_C_F_D_OP:
        case TOK_C_UN_D_OP:
        case TOK_C_EQ_D_OP:
        case TOK_C_UEQ_D_OP:
        case TOK_C_OLT_D_OP:
        case TOK_C_OLE_D_OP:
        case TOK_C_ULT_D_OP:
        case TOK_C_ULE_D_OP:
        case TOK_C_SF_D_OP:
        case TOK_C_NGLE_D_OP:
        case TOK_C_SEQ_D_OP:
        case TOK_C_NGL_D_OP:
        case TOK_C_LT_D_OP:
        case TOK_C_NGE_D_OP:
        case TOK_C_LE_D_OP:
        case TOK_C_NGT_D_OP: {
          double v1 = FPR_D(FS(inst)), v2 = FPR_D(FT(inst));
          int cond = COND(inst);
          int cc = CCFP(inst);

          if (NaN(v1) || NaN(v2)) {
            if (cond & COND_IN) {
              RAISE_EXCEPTION(ExcCode_FPE, break);
            }
            set_fpu_cc(cond, cc, 0, 0, 1);
          } else {
            set_fpu_cc(cond, cc, v1 < v2, v1 == v2, 0);
          }
        } break;

        case TOK_CFC1_OP:
          R[RT(inst)] = FCR[FS(inst)];
          break;

        case TOK_CTC1_OP:
          FCR[FS(inst)] = R[RT(inst)];

          if (FIR_REG == FS(inst)) {
            /* Read only register */
          } else if (FCSR_REG == FS(inst)) {
            if ((R[RT(inst)] & ~FCSR_MASK) != 0)
              /* Trying to set unsupported mode */
              RAISE_EXCEPTION(ExcCode_FPE, {});
          }
          break;

        case TOK_CEIL_W_D_OP: {
          double val = FPR_D(FS(inst));

          SET_FPR_W(FD(inst), (int32_t)ceil(val));
          break;
        }

        case TOK_CEIL_W_S_OP: {
          double val = (double)FPR_S(FS(inst));

          SET_FPR_W(FD(inst), (int32_t)ceil(val));
          break;
        }

        case TOK_CVT_D_S_OP: {
          double val = FPR_S(FS(inst));

          SET_FPR_D(FD(inst), val);
          break;
        }

        case TOK_CVT_D_W_OP: {
          double val = (double)FPR_W(FS(inst));

          SET_FPR_D(FD(inst), val);
          break;
        }

        case TOK_CVT_S_D_OP: {
          float val = (float)FPR_D(FS(inst));

          SET_FPR_S(FD(inst), val);
          break;
        }

        case TOK_CVT_S_W_OP: {
          float val = (float)FPR_W(FS(inst));

          SET_FPR_S(FD(inst), val);
          break;
        }

        case TOK_CVT_W_D_OP: {
          int val = (int32_t)FPR_D(FS(inst));

          SET_FPR_W(FD(inst), val);
          break;
        }

        case TOK_CVT_W_S_OP: {
          int val = (int32_t)FPR_S(FS(inst));

          SET_FPR_W(FD(inst), val);
          break;
        }

        case TOK_DIV_S_OP:
          SET_FPR_S(FD(inst), FPR_S(FS(inst)) / FPR_S(FT(inst)));
          break;

        case TOK_DIV_D_OP:
          SET_FPR_D(FD(inst), FPR_D(FS(inst)) / FPR_D(FT(inst)));
          break;

        case TOK_FLOOR_W_D_OP: {
          double val = FPR_D(FS(inst));

          SET_FPR_W(FD(inst), (int32_t)floor(val));
          break;
        }

        case TOK_FLOOR_W_S_OP: {
          double val = (double)FPR_S(FS(inst));

          SET_FPR_W(FD(inst), (int32_t)floor(val));
          break;
        }

        case TOK_LDC1_OP: {
          mem_addr addr = R[BASE(inst)] + IOFFSET(inst);
          if ((addr & 0x3) != 0)
            RAISE_EXCEPTION(ExcCode_AdEL, CP0_BadVAddr = addr);

          LOAD_INST((reg_word*)&FPR_S(FT(inst)), mem_read_word(addr),
                    0xffffffff);
          LOAD_INST((reg_word*)&FPR_S(FT(inst) + 1),
                    mem_read_word(addr + sizeof(mem_word)), 0xffffffff);
          break;
        }

        case TOK_LWC1_OP:
          LOAD_INST((reg_word*)&FPR_S(FT(inst)),
                    mem_read_word(R[BASE(inst)] + IOFFSET(inst)), 0xffffffff);
          break;

        case TOK_MFC1_OP: {
          float val = FPR_S(FS(inst));
          reg_word* vp = (reg_word*)&val;

          R[RT(inst)] = *vp; /* Fool coercion */
          break;
        }

        case TOK_MOV_S_OP:
          SET_FPR_S(FD(inst), FPR_S(FS(inst)));
          break;

        case TOK_MOV_D_OP:
          SET_FPR_D(FD(inst), FPR_D(FS(inst)));
          break;

        case TOK_MOVF_OP: {
          int cc = CC(inst);
          if (FCC(cc) == 0) R[RD(inst)] = R[RS(inst)];
          break;
        }

        case TOK_MOVF_D_OP: {
          int cc = CC(inst);
          if (FCC(cc) == 0) SET_FPR_D(FD(inst), FPR_D(FS(inst)));
          break;
        }

        case TOK_MOVF_S_OP: {
          int cc = CC(inst);
          if (FCC(cc) == 0) SET_FPR_S(FD(inst), FPR_S(FS(inst)));
          break;
        }

        case TOK_MOVN_D_OP: {
          if (R[RT(inst)] != 0) SET_FPR_D(FD(inst), FPR_D(FS(inst)));
          break;
        }

        case TOK_MOVN_S_OP: {
          if (R[RT(inst)] != 0) SET_FPR_S(FD(inst), FPR_S(FS(inst)));
          break;
        }

        case TOK_MOVT_OP: {
          int cc = CC(inst);
          if (FCC(cc) != 0) R[RD(inst)] = R[RS(inst)];
          break;
        }

        case TOK_MOVT_D_OP: {
          int cc = CC(inst);
          if (FCC(cc) != 0) SET_FPR_D(FD(inst), FPR_D(FS(inst)));
          break;
        }

        case TOK_MOVT_S_OP: {
          int cc = CC(inst);
          if (FCC(cc) != 0) SET_FPR_S(FD(inst), FPR_S(FS(inst)));
          break;
        }

        case TOK_MOVZ_D_OP: {
          if (R[RT(inst)] == 0) SET_FPR_D(FD(inst), FPR_D(FS(inst)));
          break;
        }

        case TOK_MOVZ_S_OP: {
          if (R[RT(inst)] == 0) SET_FPR_S(FD(inst), FPR_S(FS(inst)));
          break;
        }

        case TOK_MTC1_OP: {
          reg_word word = R[RT(inst)];
          float* wp = (float*)&word;

          SET_FPR_S(FS(inst), *wp); /* fool coercion */
          break;
        }

        case TOK_MUL_S_OP:
          SET_FPR_S(FD(inst), FPR_S(FS(inst)) * FPR_S(FT(inst)));
          break;

        case TOK_MUL_D_OP:
          SET_FPR_D(FD(inst), FPR_D(FS(inst)) * FPR_D(FT(inst)));
          break;

        case TOK_NEG_S_OP:
          SET_FPR_S(FD(inst), -FPR_S(FS(inst)));
          break;

        case TOK_NEG_D_OP:
          SET_FPR_D(FD(inst), -FPR_D(FS(inst)));
          break;

        case TOK_ROUND_W_D_OP: {
          double val = FPR_D(FS(inst));

          SET_FPR_W(FD(inst), (int32_t)(val + 0.5)); /* Casting truncates */
          break;
        }

        case TOK_ROUND_W_S_OP: {
          double val = (double)FPR_S(FS(inst));

          SET_FPR_W(FD(inst), (int32_t)(val + 0.5)); /* Casting truncates */
          break;
        }

        case TOK_SDC1_OP: {
          double val = FPR_D(RT(inst));
          reg_word* vp = (reg_word*)&val;
          mem_addr addr = R[BASE(inst)] + IOFFSET(inst);
          if ((addr & 0x3) != 0)
            RAISE_EXCEPTION(ExcCode_AdEL, CP0_BadVAddr = addr);

          mem_write_word(addr, *vp);
          mem_write_word(addr + sizeof(mem_word), *(vp + 1));
          break;
        }

        case TOK_SQRT_D_OP:
          SET_FPR_D(FD(inst), sqrt(FPR_D(FS(inst))));
          break;

        case TOK_SQRT_S_OP:
          SET_FPR_S(FD(inst), sqrt(FPR_S(FS(inst))));
          break;

        case TOK_SUB_S_OP:
          SET_FPR_S(FD(inst), FPR_S(FS(inst)) - FPR_S(FT(inst)));
          break;

        case TOK_SUB_D_OP:
          SET_FPR_D(FD(inst), FPR_D(FS(inst)) - FPR_D(FT(inst)));
          break;

        case TOK_SWC1_OP: {
          float val = FPR_S(RT(inst));
          reg_word* vp = (reg_word*)&val;

          mem_write_word(R[BASE(inst)] + IOFFSET(inst), *vp);
          break;
        }

        case TOK_TRUNC_W_D_OP: {
          double val = FPR_D(FS(inst));

          SET_FPR_W(FD(inst), (int32_t)val); /* Casting truncates */
          break;
        }

        case TOK_TRUNC_W_S_OP: {
          double val = (double)FPR_S(FS(inst));

          SET_FPR_W(FD(inst), (int32_t)val); /* Casting truncates */
          break;
        }

        default:
          fatal_error("Unknown instruction type: %d\n", OPCODE(inst));
          break;
      }

      /* After instruction executes: */
      PC += BYTES_PER_WORD;

      explain_after(inst);

      if (display) print_inst(PC);

      if (exception_occurred) {
        handle_exception();
      }
    } /* End: for (step = 0; ... */
  } /* End: for ( ; steps_to_run > 0 ... */

  /* Executed enought steps, return, but are able to continue. */
  return true;
}

/* Increment CP0 Count register and test if it matches the Compare
   register. If so, cause an interrupt. */

static void bump_CP0_timer(void) {
  CP0_Count += 1;
  if (CP0_Count == CP0_Compare) {
    RAISE_INTERRUPT(7);
  }
}

static void start_CP0_timer(void) {
  /* Read the timer with getitimer rather than handling SIGALRM, since signals
     interrupt I/O calls (read, etc.) and make user interaction with SPIM work
     very poorly. Since speed isn't an important aspect of SPIM, polling isn't
     a big deal. */
  if (SIG_ERR == signal(SIGALRM, SIG_IGN)) {
    perror("signal failed");
  } else {
    struct itimerval time;
    if (-1 == getitimer(ITIMER_REAL, &time)) {
      perror("getitmer failed");
    }
    if (time.it_value.tv_usec == 0 && time.it_value.tv_sec == 0) {
      /* Timer is expired or has not been started.
         Start a non-periodic timer for TIMER_TICK_MS microseconds. */
      time.it_interval.tv_sec = 0;
      time.it_interval.tv_usec = 0;
      time.it_value.tv_sec = 0;
      time.it_value.tv_usec = TIMER_TICK_MS * 1000;
      if (-1 == setitimer(ITIMER_REAL, &time, nullptr)) {
        perror("setitmer failed");
      }
    }
  }
}

/* Multiply two 32-bit numbers, V1 and V2, to produce a 64 bit result in
   the HI/LO registers.	 The algorithm is high-school math:

         A B
       x C D
       ------
       AD || BD
 AC || CB || 0

 where A and B are the high and low short words of V1, C and D are the short
 words of V2, AD is the product of A and D, and X || Y is (X << 16) + Y.
 Since the algorithm is programmed in C, we need to be careful not to
 overflow. */

static void unsigned_multiply(reg_word v1, reg_word v2) {
  u_reg_word a, b, c, d;
  u_reg_word bd, ad, cb, ac;
  u_reg_word mid, mid2, carry_mid = 0;

  a = (v1 >> 16) & 0xffff;
  b = v1 & 0xffff;
  c = (v2 >> 16) & 0xffff;
  d = v2 & 0xffff;

  bd = b * d;
  ad = a * d;
  cb = c * b;
  ac = a * c;

  mid = ad + cb;
  if (mid < ad || mid < cb) /* Arithmetic overflow or carry-out */
    carry_mid = 1;

  mid2 = mid + ((bd >> 16) & 0xffff);
  if (mid2 < mid || mid2 < ((bd >> 16) & 0xffff))
    /* Arithmetic overflow or carry-out */
    carry_mid += 1;

  LO = (bd & 0xffff) | ((mid2 & 0xffff) << 16);
  HI = ac + (carry_mid << 16) + ((mid2 >> 16) & 0xffff);
}

static void signed_multiply(reg_word v1, reg_word v2) {
  int neg_sign = 0;

  if (v1 < 0) {
    v1 = -v1;
    neg_sign = 1;
  }
  if (v2 < 0) {
    v2 = -v2;
    neg_sign = !neg_sign;
  }

  unsigned_multiply(v1, v2);
  if (neg_sign) {
    LO = ~LO;
    HI = ~HI;
    LO += 1;
    if (LO == 0) HI += 1;
  }
}

static void set_fpu_cc(int cond, int cc, int less, int equal, int unordered) {
  int result = 0;

  if (cond & COND_LT) result |= less;
  if (cond & COND_EQ) result |= equal;
  if (cond & COND_UN) result |= unordered;

  SET_FCC(cc, result);
}

void raise_exception(int excode) {
  if (ExcCode_Int != excode ||
      ((CP0_Status & CP0_Status_IE) /* Allow interrupt if IE and !EXL */
       && !(CP0_Status & CP0_Status_EXL))) {
    /* Ignore interrupt exception when interrupts disabled.  */
    exception_occurred = 1;
    /* Record the FIRST "bad" exception — anything other than a normal-flow
       interrupt (0), syscall (8), or breakpoint (9).  Used by main() to
       set a non-zero shell exit status when the user program took a fault.
       Only the first wins because spim's default handler advances past the
       faulting instruction and continues; without the latch, a misbehaving
       loop would overwrite the status repeatedly. */
    if (first_bad_exception == -1 && excode != ExcCode_Int &&
        excode != ExcCode_Sys && excode != ExcCode_Bp) {
      first_bad_exception = excode;
    }
    if (running_in_delay_slot) {
      /* In delay slot */
      if ((CP0_Status & CP0_Status_EXL) == 0) {
        /* Branch's addr */
        CP0_EPC = ROUND_DOWN(PC - BYTES_PER_WORD, BYTES_PER_WORD);
        /* Set BD bit to record that instruction is in delay slot */
        CP0_Cause |= CP0_Cause_BD;
      }
    } else {
      /* Not in delay slot */
      if ((CP0_Status & CP0_Status_EXL) == 0) {
        /* Faulting instruction's address */
        CP0_EPC = ROUND_DOWN(PC, BYTES_PER_WORD);
      }
    }
    /* ToDo: set CE field of Cause register to coprocessor causing exception */

    /* Record cause of exception */
    CP0_Cause = (CP0_Cause & ~CP0_Cause_ExcCode) | (excode << 2);

    /* Turn on EXL bit to prevent subsequent interrupts from affecting EPC */
    CP0_Status |= CP0_Status_EXL;

#ifdef MIPS1
    CP0_Status = (CP0_Status & 0xffffffc0) | ((CP0_Status & 0xf) << 2);
#endif
  }
}
