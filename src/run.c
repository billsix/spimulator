/* SPIM S20 MIPS simulator.
   Execute SPIM instructions.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

#include <math.h>
#include <stdio.h>

#include <errno.h>
#include <stdbit.h>
#include <stdckdint.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>

#include "spim.h"
#include "string-stream.h"
#include "spim-utils.h"
#include "instruction.h"
#include "registers.h"
#include "memory.h"
#include "symbol-table.h"
#include "tokens.h"
#include "syscall.h"
#include "run.h"
#include "explain.h"

bool force_break =
    false; /* For the execution env. to force an execution break */

/* Local functions: */

static void bump_coprocessor_0_timer(void);
static void set_floating_point_unit_condition_code(int condition, int condition_code, int less, int equal, int unordered);
static void signed_multiply(register_word v1, register_word v2);
static void start_coprocessor_0_timer(void);
static void unsigned_multiply(register_word v1, register_word v2);

/* Extract bit 31 — used by the sign-aware branch instructions
   (bgez/bgtz/blez/bltz and their AL/L variants).  For arithmetic
   overflow detection on add/addi/sub, see ckd_add/ckd_sub from
   <stdckdint.h> at the relevant case bodies. */
static inline unsigned_register_word sign_bit(unsigned_register_word x) { return x & 0x80000000; }

/* True when delayed_branches is true and instruction is executing in delay
slot of another instruction. */
static bool running_in_delay_slot = false;

/* Executed delayed branch and jump instructions by running the
   instruction from the delay slot before transfering control.  Note,
   in branches that don't jump, the instruction in the delay slot is
   executed by falling through normally.

   We take advantage of the MIPS architecture, which leaves undefined
   the result of executing a delayed instruction in a delay slot.  Here
   we execute the second branch. */

#define BRANCH_INSTRUCTION(TEST, TARGET, NULLIFY)       \
  do {                                           \
    if (TEST) {                                  \
      memory_address target = (TARGET);                \
      if (delayed_branches) {                    \
        /* +4 since jump in delay slot */        \
        target += BYTES_PER_WORD;                \
      }                                          \
      JUMP_INSTRUCTION(target);                         \
    } else if (NULLIFY) {                        \
      /* If test fails and nullify bit set, skip \
         instruction in delay slot. */           \
      PROGRAM_COUNTER += BYTES_PER_WORD;                      \
    } else {                                     \
      /* Not taken and not nullified: the delay  \
         slot runs as the next instruction. */   \
    }                                            \
  } while (0)

#define JUMP_INSTRUCTION(TARGET)                                           \
  do {                                                              \
    if (delayed_branches) {                                         \
      const explain_snapshot branch_snapshot = explain_save_snapshot(); \
      running_in_delay_slot = true;                                 \
      /* Continuation flag from the delay-slot instruction is       \
         discarded — the outer run_spim owns that. */             \
      (void)run_spim(PROGRAM_COUNTER + BYTES_PER_WORD, 1, display);              \
      running_in_delay_slot = false;                                \
      explain_restore_snapshot(&branch_snapshot);                       \
    }                                                               \
    /* -4 since PC is bumped after this instruction */              \
    PROGRAM_COUNTER = (TARGET) - BYTES_PER_WORD;                                 \
  } while (0)

/* If the delayed_load flag is false, the result from a load is available
   immediate.  If the delayed_load flag is true, the result from a load is
   not available until the subsequent instruction has executed (as in the
   real machine). We need a two element shift register for the value and its
   destination, as the instruction following the load can itself be a load
   instruction. */

#define LOAD_INSTRUCTION(DEST_A, LD, MASK)        \
  do {                                     \
    LOAD_INSTRUCTION_BASE(DEST_A, (LD & (MASK))); \
  } while (0)

#define LOAD_INSTRUCTION_BASE(DEST_A, VALUE) \
  do {                                \
    if (delayed_loads) {              \
      delayed_load_address1 = (DEST_A);  \
      delayed_load_value1 = (VALUE);  \
    } else {                          \
      *(DEST_A) = (VALUE);            \
    }                                 \
  } while (0)

#define DO_DELAYED_UPDATE()                        \
  do {                                             \
    if (delayed_loads) {                           \
      /* Check for delayed updates */              \
      if (delayed_load_address2 != nullptr) {         \
        *delayed_load_address2 = delayed_load_value2; \
      }                                            \
      delayed_load_address2 = delayed_load_address1;     \
      delayed_load_value2 = delayed_load_value1;   \
      delayed_load_address1 = nullptr;                \
    }                                              \
  } while (0)

/* Run the program stored in memory, starting at address PC for
   STEPS_TO_RUN instruction executions.  If flag DISPLAY is true, print
   each instruction before it executes. Return true if program's
   execution can continue. */

bool run_spim(memory_address initial_program_counter, int steps_to_run, bool display) {
  mips_instruction* instruction;
  static register_word *delayed_load_address1 = nullptr, delayed_load_value1;
  static register_word *delayed_load_address2 = nullptr, delayed_load_value2;
  int step_size;

  PROGRAM_COUNTER = initial_program_counter;
  /* Without mapped IO, run to completion in one chunk. */
  const int next_step =
      (!bare_machine && mapped_io) ? IO_INTERVAL : steps_to_run;

  /* Start a timer running */
  start_coprocessor_0_timer();

  for (step_size = MIN(next_step, steps_to_run); steps_to_run > 0;
       steps_to_run -= step_size, step_size = MIN(next_step, steps_to_run)) {
    if (!bare_machine && mapped_io)
      /* Every IO_INTERVAL steps, check if memory-mapped IO registers
         have changed. */
      check_memory_mapped_IO();
    /* else run inner loop for all steps */

    if (((uint32_t)coprocessor_0_status & COPROCESSOR_0_STATUS_INTERRUPT_ENABLE) &&
        !((uint32_t)coprocessor_0_status & COPROCESSOR_0_STATUS_EXCEPTION_LEVEL) &&
        (((uint32_t)coprocessor_0_cause & COPROCESSOR_0_CAUSE_INTERRUPTS_PENDING) &
         ((uint32_t)coprocessor_0_status & COPROCESSOR_0_STATUS_INTERRUPT_MASK))) {
      /* There is an interrupt to process if IE bit set, EXL bit not
         set, and non-masked IP bit set */
      raise_exception(EXCEPTION_CODE_INTERRUPT);
      /* Handle interrupt now, before instruction executes, so that
         EPC points to unexecuted instructions, which is the one to
         return to. */
      handle_exception();
    }

    force_break = false;
    for (int step = 0; step < step_size; step += 1) {
      if (force_break) {
        return true;
      }

      general_purpose_registers[0] = 0; /* Maintain invariant value */

      {
        /* Poll for timer expiration */
        struct itimerval time = {};
        if (-1 == getitimer(ITIMER_REAL, &time)) {
          perror("getitmer failed");
        }
        if (time.it_value.tv_usec == 0 && time.it_value.tv_sec == 0) {
          /* Timer expired */
          bump_coprocessor_0_timer();

          /* Restart timer for next interval */
          start_coprocessor_0_timer();
        }
      }

      exception_occurred = false;
      instruction = memory_read_instruction(PROGRAM_COUNTER);
      /* Every branch of this chain leaves, so the rest of the loop body is its
         else; nesting all of that one level deeper would read worse. */
      /* dispatch-check: ok -- see the comment above */
      if (exception_occurred) /* In reading instruction */
      {
        exception_occurred = false;
        handle_exception();
        continue;
      } else if (instruction == nullptr) {
        run_error("Attempt to execute non-instruction at 0x%08x\n", PROGRAM_COUNTER);
        return false;
      } else if (EXPRESSION(instruction) != nullptr &&
                 EXPRESSION(instruction)->symbol != nullptr &&
                 EXPRESSION(instruction)->symbol->address == 0) {
        run_error("Instruction references undefined symbol at 0x%08x\n  %s", PROGRAM_COUNTER,
                  instruction_to_string(PROGRAM_COUNTER));
        return false;
      }

#ifdef TEST_ASM
      test_assembly(instruction);
#endif

      DO_DELAYED_UPDATE();

      explain_before(instruction, PROGRAM_COUNTER);

      switch (OPCODE(instruction)) {
        case TOKEN_ADD_OPCODE: {
          register_word sum;
          if (ckd_add(&sum, general_purpose_registers[SOURCE_REGISTER(instruction)], general_purpose_registers[TARGET_REGISTER(instruction)]))
            RAISE_EXCEPTION(EXCEPTION_CODE_ARITHMETIC_OVERFLOW, break);
          general_purpose_registers[DESTINATION_REGISTER(instruction)] = sum;
          break;
        }

        case TOKEN_ADDI_OPCODE: {
          register_word sum;
          if (ckd_add(&sum, general_purpose_registers[SOURCE_REGISTER(instruction)],
                      (register_word)(short)IMMEDIATE(instruction)))
            RAISE_EXCEPTION(EXCEPTION_CODE_ARITHMETIC_OVERFLOW, break);
          general_purpose_registers[TARGET_REGISTER(instruction)] = sum;
          break;
        }

        case TOKEN_ADDIU_OPCODE:
          general_purpose_registers[TARGET_REGISTER(instruction)] =
              (register_word)((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] +
                         (unsigned_register_word)(short)IMMEDIATE(instruction));
          break;

        case TOKEN_ADDU_OPCODE:
          general_purpose_registers[DESTINATION_REGISTER(instruction)] = (register_word)((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] +
                                            (unsigned_register_word)general_purpose_registers[TARGET_REGISTER(instruction)]);
          break;

        case TOKEN_AND_OPCODE:
          general_purpose_registers[DESTINATION_REGISTER(instruction)] = general_purpose_registers[SOURCE_REGISTER(instruction)] & general_purpose_registers[TARGET_REGISTER(instruction)];
          break;

        case TOKEN_ANDI_OPCODE:
          general_purpose_registers[TARGET_REGISTER(instruction)] =
              general_purpose_registers[SOURCE_REGISTER(instruction)] & (0xffff & IMMEDIATE(instruction));
          break;

        case TOKEN_BC2F_OPCODE:
        case TOKEN_BC2FL_OPCODE:
        case TOKEN_BC2T_OPCODE:
        case TOKEN_BC2TL_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_COPROCESSOR_UNUSABLE, {}); /* No Coprocessor 2 */
          break;

        case TOKEN_BEQ_OPCODE:
          BRANCH_INSTRUCTION(general_purpose_registers[SOURCE_REGISTER(instruction)] == general_purpose_registers[TARGET_REGISTER(instruction)],
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 0);
          break;

        case TOKEN_BEQL_OPCODE:
          BRANCH_INSTRUCTION(general_purpose_registers[SOURCE_REGISTER(instruction)] == general_purpose_registers[TARGET_REGISTER(instruction)],
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 1);
          break;

        case TOKEN_BGEZ_OPCODE:
          BRANCH_INSTRUCTION(sign_bit((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)]) == 0,
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 0);
          break;

        case TOKEN_BGEZL_OPCODE:
          BRANCH_INSTRUCTION(sign_bit((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)]) == 0,
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 1);
          break;

        case TOKEN_BGEZAL_OPCODE:
          general_purpose_registers[31] = (register_word)(PROGRAM_COUNTER + (delayed_branches ? 2 * BYTES_PER_WORD
                                                      : BYTES_PER_WORD));
          BRANCH_INSTRUCTION(sign_bit((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)]) == 0,
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 0);
          break;

        case TOKEN_BGEZALL_OPCODE:
          general_purpose_registers[31] = (register_word)(PROGRAM_COUNTER + (delayed_branches ? 2 * BYTES_PER_WORD
                                                      : BYTES_PER_WORD));
          BRANCH_INSTRUCTION(sign_bit((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)]) == 0,
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 1);
          break;

        case TOKEN_BGTZ_OPCODE:
          BRANCH_INSTRUCTION(general_purpose_registers[SOURCE_REGISTER(instruction)] != 0 &&
                          sign_bit((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)]) == 0,
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 0);
          break;

        case TOKEN_BGTZL_OPCODE:
          BRANCH_INSTRUCTION(general_purpose_registers[SOURCE_REGISTER(instruction)] != 0 &&
                          sign_bit((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)]) == 0,
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 1);
          break;

        case TOKEN_BLEZ_OPCODE:
          BRANCH_INSTRUCTION(general_purpose_registers[SOURCE_REGISTER(instruction)] == 0 ||
                          sign_bit((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)]) != 0,
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 0);
          break;

        case TOKEN_BLEZL_OPCODE:
          BRANCH_INSTRUCTION(general_purpose_registers[SOURCE_REGISTER(instruction)] == 0 ||
                          sign_bit((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)]) != 0,
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 1);
          break;

        case TOKEN_BLTZ_OPCODE:
          BRANCH_INSTRUCTION(sign_bit((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)]) != 0,
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 0);
          break;

        case TOKEN_BLTZL_OPCODE:
          BRANCH_INSTRUCTION(sign_bit((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)]) != 0,
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 1);
          break;

        case TOKEN_BLTZAL_OPCODE:
          general_purpose_registers[31] = (register_word)(PROGRAM_COUNTER + (delayed_branches ? 2 * BYTES_PER_WORD
                                                      : BYTES_PER_WORD));
          BRANCH_INSTRUCTION(sign_bit((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)]) != 0,
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 0);
          break;

        case TOKEN_BLTZALL_OPCODE:
          general_purpose_registers[31] = (register_word)(PROGRAM_COUNTER + (delayed_branches ? 2 * BYTES_PER_WORD
                                                      : BYTES_PER_WORD));
          BRANCH_INSTRUCTION(sign_bit((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)]) != 0,
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 1);
          break;

        case TOKEN_BNE_OPCODE:
          BRANCH_INSTRUCTION(general_purpose_registers[SOURCE_REGISTER(instruction)] != general_purpose_registers[TARGET_REGISTER(instruction)],
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 0);
          break;

        case TOKEN_BNEL_OPCODE:
          BRANCH_INSTRUCTION(general_purpose_registers[SOURCE_REGISTER(instruction)] != general_purpose_registers[TARGET_REGISTER(instruction)],
                      PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction), 1);
          break;

        case TOKEN_BREAK_OPCODE:
          if (DESTINATION_REGISTER(instruction) == 1) /* Debugger breakpoint */
            RAISE_EXCEPTION(EXCEPTION_CODE_BREAKPOINT, return true)
          else
            RAISE_EXCEPTION(EXCEPTION_CODE_BREAKPOINT, break);

        case TOKEN_CACHE_OPCODE:
          break; /* Memory details not implemented */

        case TOKEN_CFC0_OPCODE:
          general_purpose_registers[TARGET_REGISTER(instruction)] =
              coprocessor_control_registers[0][DESTINATION_REGISTER(instruction)];
          break;

        case TOKEN_CFC2_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_COPROCESSOR_UNUSABLE, {}); /* No Coprocessor 2 */
          break;

        case TOKEN_CLO_OPCODE:
          general_purpose_registers[DESTINATION_REGISTER(instruction)] =
              (register_word)stdc_leading_ones((uint32_t)general_purpose_registers[SOURCE_REGISTER(instruction)]);
          break;

        case TOKEN_CLZ_OPCODE:
          general_purpose_registers[DESTINATION_REGISTER(instruction)] =
              (register_word)stdc_leading_zeros((uint32_t)general_purpose_registers[SOURCE_REGISTER(instruction)]);
          break;

        case TOKEN_COP2_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_COPROCESSOR_UNUSABLE, {}); /* No Coprocessor 2 */
          break;

        case TOKEN_CTC0_OPCODE:
          coprocessor_control_registers[0][DESTINATION_REGISTER(instruction)] =
              general_purpose_registers[TARGET_REGISTER(instruction)];
          break;

        case TOKEN_CTC2_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_COPROCESSOR_UNUSABLE, {}); /* No Coprocessor 2 */
          break;

        case TOKEN_DIV_OPCODE:
          /* The behavior of this instruction is undefined on divide by
             zero or overflow. */
          if (general_purpose_registers[TARGET_REGISTER(instruction)] != 0 &&
              !(general_purpose_registers[SOURCE_REGISTER(instruction)] == (register_word)0x80000000 &&
                general_purpose_registers[TARGET_REGISTER(instruction)] == (register_word)0xffffffff)) {
            MULTIPLY_DIVIDE_LOW =
                (register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] / (register_word)general_purpose_registers[TARGET_REGISTER(instruction)];
            MULTIPLY_DIVIDE_HIGH =
                (register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] % (register_word)general_purpose_registers[TARGET_REGISTER(instruction)];
          }
          break;

        case TOKEN_DIVU_OPCODE:
          /* The behavior of this instruction is undefined on divide by
             zero.  Unsigned division cannot overflow. */
          if (general_purpose_registers[TARGET_REGISTER(instruction)] != 0) {
            MULTIPLY_DIVIDE_LOW = (register_word)((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] /
                            (unsigned_register_word)general_purpose_registers[TARGET_REGISTER(instruction)]);
            MULTIPLY_DIVIDE_HIGH = (register_word)((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] %
                            (unsigned_register_word)general_purpose_registers[TARGET_REGISTER(instruction)]);
          }
          break;

        case TOKEN_ERET_OPCODE: {
          coprocessor_0_status = (register_word)((unsigned_register_word)coprocessor_0_status &
                                  ~COPROCESSOR_0_STATUS_EXCEPTION_LEVEL); /* Clear EXL bit */
          JUMP_INSTRUCTION((memory_address)coprocessor_0_exception_program_counter);             /* Jump to EPC */
        } break;

        case TOKEN_J_OPCODE:
          JUMP_INSTRUCTION(((PROGRAM_COUNTER & 0xf0000000) | TARGET(instruction) << 2));
          break;

        case TOKEN_JAL_OPCODE:
          if (delayed_branches)
            general_purpose_registers[31] = (register_word)(PROGRAM_COUNTER + 2 * BYTES_PER_WORD);
          else
            general_purpose_registers[31] = (register_word)(PROGRAM_COUNTER + BYTES_PER_WORD);
          JUMP_INSTRUCTION(((PROGRAM_COUNTER & 0xf0000000) | (TARGET(instruction) << 2)));
          break;

        /* The hazard-barrier forms (.hb) run as the plain ones: the simulator
           has no pipeline hazards for a barrier to clear. */
        case TOKEN_JALR_HB_OPCODE:
        case TOKEN_JALR_OPCODE: {
          const memory_address temporary = (memory_address)general_purpose_registers[SOURCE_REGISTER(instruction)];

          if (delayed_branches)
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = (register_word)(PROGRAM_COUNTER + 2 * BYTES_PER_WORD);
          else
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = (register_word)(PROGRAM_COUNTER + BYTES_PER_WORD);
          JUMP_INSTRUCTION(temporary);
        } break;

        case TOKEN_JR_HB_OPCODE:
        case TOKEN_JR_OPCODE: {
          const memory_address temporary = (memory_address)general_purpose_registers[SOURCE_REGISTER(instruction)];

          JUMP_INSTRUCTION(temporary);
        } break;

        case TOKEN_LB_OPCODE:
          LOAD_INSTRUCTION(&general_purpose_registers[TARGET_REGISTER(instruction)],
                    memory_read_byte((unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                  (unsigned_register_word)IMMEDIATE_OFFSET(instruction)),
                    (register_word)0xffffffff);
          break;

        case TOKEN_LBU_OPCODE:
          LOAD_INSTRUCTION(&general_purpose_registers[TARGET_REGISTER(instruction)],
                    memory_read_byte((unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                  (unsigned_register_word)IMMEDIATE_OFFSET(instruction)),
                    0xff);
          break;

        case TOKEN_LH_OPCODE:
          LOAD_INSTRUCTION(&general_purpose_registers[TARGET_REGISTER(instruction)],
                    memory_read_half((unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                  (unsigned_register_word)IMMEDIATE_OFFSET(instruction)),
                    (register_word)0xffffffff);
          break;

        case TOKEN_LHU_OPCODE:
          LOAD_INSTRUCTION(&general_purpose_registers[TARGET_REGISTER(instruction)],
                    memory_read_half((unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                  (unsigned_register_word)IMMEDIATE_OFFSET(instruction)),
                    0xffff);
          break;

        case TOKEN_LL_OPCODE:
          /* Uniprocess, so this instruction is just a load */
          LOAD_INSTRUCTION(&general_purpose_registers[TARGET_REGISTER(instruction)],
                    memory_read_word((unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                  (unsigned_register_word)IMMEDIATE_OFFSET(instruction)),
                    (register_word)0xffffffff);
          break;

        case TOKEN_LUI_OPCODE:
          general_purpose_registers[TARGET_REGISTER(instruction)] =
              (register_word)(((unsigned)IMMEDIATE(instruction) << 16) & 0xffff0000);
          break;

        case TOKEN_LW_OPCODE:
          LOAD_INSTRUCTION(&general_purpose_registers[TARGET_REGISTER(instruction)],
                    memory_read_word((unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                  (unsigned_register_word)IMMEDIATE_OFFSET(instruction)),
                    (register_word)0xffffffff);
          break;

        case TOKEN_LDC2_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_COPROCESSOR_UNUSABLE, {}); /* No Coprocessor 2 */
          break;

        case TOKEN_LWC2_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_COPROCESSOR_UNUSABLE, {}); /* No Coprocessor 2 */
          break;

        case TOKEN_LWL_OPCODE: {
          const memory_address address = (unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                (unsigned_register_word)IMMEDIATE_OFFSET(instruction);
          const int byte = address & 0x3;
          const register_word register_value = general_purpose_registers[TARGET_REGISTER(instruction)];
          register_word word = memory_read_word(address & 0xfffffffc);
          if (!exception_occurred)
#ifdef SPIM_BIGENDIAN
            switch (byte) {
              case 0:
                word = word;
                break;

              case 1:
                word = ((unsigned)(word & 0xffffff) << 8) | (register_value & 0xff);
                break;

              case 2:
                word = ((unsigned)(word & 0xffff) << 16) | (register_value & 0xffff);
                break;

              case 3:
                word = ((unsigned)(word & 0xff) << 24) | (register_value & 0xffffff);
                break;
            }
#else
            switch (byte) {
              case 0:
                word = (register_word)(((unsigned)(word & 0xff) << 24) |
                                  (register_value & 0xffffff));
                break;

              case 1:
                word = (register_word)(((unsigned)(word & 0xffff) << 16) |
                                  (register_value & 0xffff));
                break;

              case 2:
                word = (register_word)(((unsigned)(word & 0xffffff) << 8) |
                                  (register_value & 0xff));
                break;

              case 3:
                break;
              default:
                /* Never reached: byte is addr & 0x3, which is 0 to 3, and each
                 * of those has a case. */
                fatal_error("internal error: run_spim: LWL byte lane %d\n",
                            byte);
            }
#endif
          LOAD_INSTRUCTION_BASE(&general_purpose_registers[TARGET_REGISTER(instruction)], word);
          break;
        }

        case TOKEN_LWR_OPCODE: {
          const memory_address address = (unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                (unsigned_register_word)IMMEDIATE_OFFSET(instruction);
          const int byte = address & 0x3;
          const register_word register_value = general_purpose_registers[TARGET_REGISTER(instruction)];
          register_word word = memory_read_word(address & 0xfffffffc);
          if (!exception_occurred)
#ifdef SPIM_BIGENDIAN
            switch (byte) {
              case 0:
                word = (register_value & 0xffffff00) |
                       ((unsigned)(word & 0xff000000) >> 24);
                break;

              case 1:
                word = (register_value & 0xffff0000) |
                       ((unsigned)(word & 0xffff0000) >> 16);
                break;

              case 2:
                word = (register_value & 0xff000000) |
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
                word = (register_word)(((unsigned_register_word)register_value & 0xff000000) |
                                  (((unsigned_register_word)word & 0xffffff00) >> 8));
                break;

              case 2:
                word = (register_word)(((unsigned_register_word)register_value & 0xffff0000) |
                                  (((unsigned_register_word)word & 0xffff0000) >> 16));
                break;

              case 3:
                word = (register_word)(((unsigned_register_word)register_value & 0xffffff00) |
                                  (((unsigned_register_word)word & 0xff000000) >> 24));
                break;
              default:
                /* Never reached: byte is addr & 0x3, which is 0 to 3, and each
                 * of those has a case. */
                fatal_error("internal error: run_spim: LWR byte lane %d\n",
                            byte);
            }
#endif
          LOAD_INSTRUCTION_BASE(&general_purpose_registers[TARGET_REGISTER(instruction)], word);
          break;
        }

        case TOKEN_MADD_OPCODE:
        case TOKEN_MADDU_OPCODE: {
          const unsigned_register_word product_low = (unsigned_register_word)MULTIPLY_DIVIDE_LOW;
          unsigned_register_word product_high = (unsigned_register_word)MULTIPLY_DIVIDE_HIGH;
          if (OPCODE(instruction) == TOKEN_MADD_OPCODE) {
            signed_multiply(general_purpose_registers[SOURCE_REGISTER(instruction)], general_purpose_registers[TARGET_REGISTER(instruction)]);
          } else /* TOKEN_MADDU_OPCODE */
          {
            unsigned_multiply(general_purpose_registers[SOURCE_REGISTER(instruction)], general_purpose_registers[TARGET_REGISTER(instruction)]);
          }
          const unsigned_register_word temporary = product_low + (unsigned_register_word)MULTIPLY_DIVIDE_LOW;
          if ((unsigned)temporary < (unsigned)MULTIPLY_DIVIDE_LOW ||
              (unsigned)temporary < (unsigned)product_low) {
            /* Addition of low-order word overflows */
            product_high += 1;
          }
          MULTIPLY_DIVIDE_LOW = (register_word)temporary;
          MULTIPLY_DIVIDE_HIGH = (register_word)(product_high + (unsigned_register_word)MULTIPLY_DIVIDE_HIGH);
          break;
        }

        case TOKEN_MFC0_OPCODE:
          general_purpose_registers[TARGET_REGISTER(instruction)] = coprocessor_registers[0][FLOATING_POINT_SOURCE(instruction)];
          break;

        case TOKEN_MFC2_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_COPROCESSOR_UNUSABLE, {}); /* No Coprocessor 2 */
          break;

        case TOKEN_MFHI_OPCODE:
          general_purpose_registers[DESTINATION_REGISTER(instruction)] = MULTIPLY_DIVIDE_HIGH;
          break;

        case TOKEN_MFLO_OPCODE:
          general_purpose_registers[DESTINATION_REGISTER(instruction)] = MULTIPLY_DIVIDE_LOW;
          break;

        case TOKEN_MOVN_OPCODE:
          if (general_purpose_registers[TARGET_REGISTER(instruction)] != 0)
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = general_purpose_registers[SOURCE_REGISTER(instruction)];
          break;

        case TOKEN_MOVZ_OPCODE:
          if (general_purpose_registers[TARGET_REGISTER(instruction)] == 0)
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = general_purpose_registers[SOURCE_REGISTER(instruction)];
          break;

        case TOKEN_MSUB_OPCODE:
        case TOKEN_MSUBU_OPCODE: {
          const unsigned_register_word product_low = (unsigned_register_word)MULTIPLY_DIVIDE_LOW;
          unsigned_register_word product_high = (unsigned_register_word)MULTIPLY_DIVIDE_HIGH;
          if (OPCODE(instruction) == TOKEN_MSUB_OPCODE) {
            signed_multiply(general_purpose_registers[SOURCE_REGISTER(instruction)], general_purpose_registers[TARGET_REGISTER(instruction)]);
          } else /* TOKEN_MSUBU_OPCODE */
          {
            unsigned_multiply(general_purpose_registers[SOURCE_REGISTER(instruction)], general_purpose_registers[TARGET_REGISTER(instruction)]);
          }

          const unsigned_register_word temporary = product_low - (unsigned_register_word)MULTIPLY_DIVIDE_LOW;
          if ((unsigned)MULTIPLY_DIVIDE_LOW > (unsigned)product_low) {
            /* Subtraction of low-order word borrows */
            product_high -= 1;
          }
          MULTIPLY_DIVIDE_LOW = (register_word)temporary;
          MULTIPLY_DIVIDE_HIGH = (register_word)(product_high - (unsigned_register_word)MULTIPLY_DIVIDE_HIGH);
          break;
        }

        case TOKEN_MTC0_OPCODE:
          coprocessor_registers[0][FLOATING_POINT_SOURCE(instruction)] = general_purpose_registers[TARGET_REGISTER(instruction)];
          switch (FLOATING_POINT_SOURCE(instruction)) {
            case COPROCESSOR_0_COMPARE_REGISTER_NUMBER:
              coprocessor_0_cause = (register_word)((unsigned_register_word)coprocessor_0_cause &
                                     ~COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_7); /* Writing clears HW
                                                         interrupt 5 */
              break;

            case COPROCESSOR_0_STATUS_REGISTER_NUMBER:
              coprocessor_0_status = (register_word)((unsigned_register_word)coprocessor_0_status & COPROCESSOR_0_STATUS_MASK);
              coprocessor_0_status =
                  (register_word)((unsigned_register_word)coprocessor_0_status |
                             ((COPROCESSOR_0_STATUS_COPROCESSOR_USABLE & 0x30000000) | COPROCESSOR_0_STATUS_USER_MODE));
              break;

            case COPROCESSOR_0_CAUSE_REGISTER_NUMBER:
              coprocessor_registers[0][FLOATING_POINT_SOURCE(instruction)] =
                  (register_word)((unsigned_register_word)
                                 coprocessor_registers[0][FLOATING_POINT_SOURCE(instruction)] &
                             COPROCESSOR_0_CAUSE_MASK);
              break;

            case COPROCESSOR_0_CONFIG_REGISTER_NUMBER:
              coprocessor_registers[0][FLOATING_POINT_SOURCE(instruction)] =
                  (register_word)((unsigned_register_word)
                                 coprocessor_registers[0][FLOATING_POINT_SOURCE(instruction)] &
                             COPROCESSOR_0_CONFIG_MASK);
              break;

            default:
              break;
          }
          break;

        case TOKEN_MTC2_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_COPROCESSOR_UNUSABLE, {}); /* No Coprocessor 2 */
          break;

        case TOKEN_MTHI_OPCODE:
          MULTIPLY_DIVIDE_HIGH = general_purpose_registers[SOURCE_REGISTER(instruction)];
          break;

        case TOKEN_MTLO_OPCODE:
          MULTIPLY_DIVIDE_LOW = general_purpose_registers[SOURCE_REGISTER(instruction)];
          break;

        case TOKEN_MUL_OPCODE:
          signed_multiply(general_purpose_registers[SOURCE_REGISTER(instruction)], general_purpose_registers[TARGET_REGISTER(instruction)]);
          general_purpose_registers[DESTINATION_REGISTER(instruction)] = MULTIPLY_DIVIDE_LOW;
          break;

        case TOKEN_MULT_OPCODE:
          signed_multiply(general_purpose_registers[SOURCE_REGISTER(instruction)], general_purpose_registers[TARGET_REGISTER(instruction)]);
          break;

        case TOKEN_MULTU_OPCODE:
          unsigned_multiply(general_purpose_registers[SOURCE_REGISTER(instruction)], general_purpose_registers[TARGET_REGISTER(instruction)]);
          break;

        case TOKEN_NOR_OPCODE:
          general_purpose_registers[DESTINATION_REGISTER(instruction)] = ~(general_purpose_registers[SOURCE_REGISTER(instruction)] | general_purpose_registers[TARGET_REGISTER(instruction)]);
          break;

        case TOKEN_OR_OPCODE:
          general_purpose_registers[DESTINATION_REGISTER(instruction)] = general_purpose_registers[SOURCE_REGISTER(instruction)] | general_purpose_registers[TARGET_REGISTER(instruction)];
          break;

        case TOKEN_ORI_OPCODE:
          general_purpose_registers[TARGET_REGISTER(instruction)] =
              general_purpose_registers[SOURCE_REGISTER(instruction)] | (0xffff & IMMEDIATE(instruction));
          break;

        case TOKEN_PREF_OPCODE:
          break; /* Memory details not implemented */

        case TOKEN_RFE_OPCODE:
#ifdef MIPS1
          /* This is MIPS-I, not compatible with MIPS32 or the
             definition of the bits in the CP0 Status register in that
             architecture. */
          coprocessor_0_status = (coprocessor_0_status & 0xfffffff0) | ((coprocessor_0_status & 0x3c) >> 2);
#else
          RAISE_EXCEPTION(EXCEPTION_CODE_RESERVED_INSTRUCTION, {}); /* Not MIPS32 instruction */
#endif
          break;

        case TOKEN_SB_OPCODE:
          memory_write_byte((unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                             (unsigned_register_word)IMMEDIATE_OFFSET(instruction),
                         general_purpose_registers[TARGET_REGISTER(instruction)]);
          break;

        case TOKEN_SC_OPCODE:
          /* Uniprocessor, so instruction is just a store */
          memory_write_word((unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                             (unsigned_register_word)IMMEDIATE_OFFSET(instruction),
                         general_purpose_registers[TARGET_REGISTER(instruction)]);
          break;

        case TOKEN_SDC2_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_COPROCESSOR_UNUSABLE, {}); /* No Coprocessor 2 */
          break;

        case TOKEN_SH_OPCODE:
          memory_write_half((unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                             (unsigned_register_word)IMMEDIATE_OFFSET(instruction),
                         general_purpose_registers[TARGET_REGISTER(instruction)]);
          break;

        case TOKEN_SLL_OPCODE: {
          const int shift_amount = SHIFT_AMOUNT(instruction);

          if (shift_amount >= 0 && shift_amount < 32)
            general_purpose_registers[DESTINATION_REGISTER(instruction)] =
                (register_word)((unsigned)general_purpose_registers[TARGET_REGISTER(instruction)] << shift_amount);
          else
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = general_purpose_registers[TARGET_REGISTER(instruction)];
          break;
        }

        case TOKEN_SLLV_OPCODE: {
          const int shift_amount = (general_purpose_registers[SOURCE_REGISTER(instruction)] & 0x1f);

          if (shift_amount >= 0 && shift_amount < 32)
            general_purpose_registers[DESTINATION_REGISTER(instruction)] =
                (register_word)((unsigned)general_purpose_registers[TARGET_REGISTER(instruction)] << shift_amount);
          else
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = general_purpose_registers[TARGET_REGISTER(instruction)];
          break;
        }

        case TOKEN_SLT_OPCODE:
          if (general_purpose_registers[SOURCE_REGISTER(instruction)] < general_purpose_registers[TARGET_REGISTER(instruction)])
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = 1;
          else
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = 0;
          break;

        case TOKEN_SLTI_OPCODE:
          if (general_purpose_registers[SOURCE_REGISTER(instruction)] < (short)IMMEDIATE(instruction))
            general_purpose_registers[TARGET_REGISTER(instruction)] = 1;
          else
            general_purpose_registers[TARGET_REGISTER(instruction)] = 0;
          break;

        case TOKEN_SLTIU_OPCODE: {
          const int x = (short)IMMEDIATE(instruction);

          if ((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] < (unsigned_register_word)x)
            general_purpose_registers[TARGET_REGISTER(instruction)] = 1;
          else
            general_purpose_registers[TARGET_REGISTER(instruction)] = 0;
          break;
        }

        case TOKEN_SLTU_OPCODE:
          if ((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] <
              (unsigned_register_word)general_purpose_registers[TARGET_REGISTER(instruction)])
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = 1;
          else
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = 0;
          break;

        case TOKEN_SRA_OPCODE: {
          const int shift_amount = SHIFT_AMOUNT(instruction);
          const register_word value = general_purpose_registers[TARGET_REGISTER(instruction)];

          if (shift_amount >= 0 && shift_amount < 32)
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = value >> shift_amount;
          else
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = value;
          break;
        }

        case TOKEN_SRAV_OPCODE: {
          const int shift_amount = general_purpose_registers[SOURCE_REGISTER(instruction)] & 0x1f;
          const register_word value = general_purpose_registers[TARGET_REGISTER(instruction)];

          if (shift_amount >= 0 && shift_amount < 32)
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = value >> shift_amount;
          else
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = value;
          break;
        }

        case TOKEN_SRL_OPCODE: {
          const int shift_amount = SHIFT_AMOUNT(instruction);
          const unsigned_register_word value = (unsigned_register_word)general_purpose_registers[TARGET_REGISTER(instruction)];

          if (shift_amount >= 0 && shift_amount < 32)
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = (register_word)(value >> shift_amount);
          else
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = (register_word)value;
          break;
        }

        case TOKEN_SRLV_OPCODE: {
          const int shift_amount = general_purpose_registers[SOURCE_REGISTER(instruction)] & 0x1f;
          const unsigned_register_word value = (unsigned_register_word)general_purpose_registers[TARGET_REGISTER(instruction)];

          if (shift_amount >= 0 && shift_amount < 32)
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = (register_word)(value >> shift_amount);
          else
            general_purpose_registers[DESTINATION_REGISTER(instruction)] = (register_word)value;
          break;
        }

        case TOKEN_SUB_OPCODE: {
          register_word diff;
          if (ckd_sub(&diff, general_purpose_registers[SOURCE_REGISTER(instruction)], general_purpose_registers[TARGET_REGISTER(instruction)]))
            RAISE_EXCEPTION(EXCEPTION_CODE_ARITHMETIC_OVERFLOW, break);
          general_purpose_registers[DESTINATION_REGISTER(instruction)] = diff;
          break;
        }

        case TOKEN_SUBU_OPCODE:
          general_purpose_registers[DESTINATION_REGISTER(instruction)] = (register_word)((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] -
                                            (unsigned_register_word)general_purpose_registers[TARGET_REGISTER(instruction)]);
          break;

        case TOKEN_SW_OPCODE:
          memory_write_word((unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                             (unsigned_register_word)IMMEDIATE_OFFSET(instruction),
                         general_purpose_registers[TARGET_REGISTER(instruction)]);
          break;

        case TOKEN_SWC2_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_COPROCESSOR_UNUSABLE, {}); /* No Coprocessor 2 */
          break;

        case TOKEN_SWL_OPCODE: {
          const memory_address address = (unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                (unsigned_register_word)IMMEDIATE_OFFSET(instruction);
          const register_word register_value = general_purpose_registers[TARGET_REGISTER(instruction)];
          const int byte = address & 0x3;
          memory_word data = memory_read_word(address & 0xfffffffc);
#ifdef SPIM_BIGENDIAN
          switch (byte) {
            case 0:
              data = register_value;
              break;

            case 1:
              data = (data & 0xff000000) | (register_value >> 8 & 0xffffff);
              break;

            case 2:
              data = (data & 0xffff0000) | (register_value >> 16 & 0xffff);
              break;

            case 3:
              data = (data & 0xffffff00) | (register_value >> 24 & 0xff);
              break;
          }
#else
          switch (byte) {
            case 0:
              data = (memory_word)(((unsigned)data & 0xffffff00) |
                                (register_value >> 24 & 0xff));
              break;

            case 1:
              data = (memory_word)(((unsigned)data & 0xffff0000) |
                                (register_value >> 16 & 0xffff));
              break;

            case 2:
              data = (memory_word)(((unsigned)data & 0xff000000) |
                                (register_value >> 8 & 0xffffff));
              break;

            case 3:
              data = register_value;
              break;
            default:
              /* Never reached: byte is addr & 0x3, which is 0 to 3, and each of
               * those has a case. */
              fatal_error("internal error: run_spim: SWL byte lane %d\n", byte);
          }
#endif
          memory_write_word(address & 0xfffffffc, data);
          break;
        }

        case TOKEN_SWR_OPCODE: {
          const memory_address address = (unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                (unsigned_register_word)IMMEDIATE_OFFSET(instruction);
          const register_word register_value = general_purpose_registers[TARGET_REGISTER(instruction)];
          const int byte = address & 0x3;
          memory_word data = memory_read_word(address & 0xfffffffc);
#ifdef SPIM_BIGENDIAN
          switch (byte) {
            case 0:
              data = (((unsigned)register_value << 24) & 0xff000000) | (data & 0xffffff);
              break;

            case 1:
              data = (((unsigned)register_value << 16) & 0xffff0000) | (data & 0xffff);
              break;

            case 2:
              data = (((unsigned)register_value << 8) & 0xffffff00) | (data & 0xff);
              break;

            case 3:
              data = register_value;
              break;
          }
#else
          switch (byte) {
            case 0:
              data = register_value;
              break;

            case 1:
              data = (memory_word)((((unsigned)register_value << 8) & 0xffffff00) |
                                (data & 0xff));
              break;

            case 2:
              data = (memory_word)((((unsigned)register_value << 16) & 0xffff0000) |
                                (data & 0xffff));
              break;

            case 3:
              data = (memory_word)((((unsigned)register_value << 24) & 0xff000000) |
                                (data & 0xffffff));
              break;
            default:
              /* Never reached: byte is addr & 0x3, which is 0 to 3, and each of
               * those has a case. */
              fatal_error("internal error: run_spim: SWR byte lane %d\n", byte);
          }
#endif
          memory_write_word(address & 0xfffffffc, data);
          break;
        }

        case TOKEN_SYNC_OPCODE:
          break; /* Memory details not implemented */

        case TOKEN_SYSCALL_OPCODE:
          if (!do_syscall()) {
            explain_program_exit(instruction);
            return false;
          }
          break;

        case TOKEN_TEQ_OPCODE:
          if (general_purpose_registers[SOURCE_REGISTER(instruction)] == general_purpose_registers[TARGET_REGISTER(instruction)])
            RAISE_EXCEPTION(EXCEPTION_CODE_TRAP, {});
          break;

        case TOKEN_TEQI_OPCODE:
          if (general_purpose_registers[SOURCE_REGISTER(instruction)] == IMMEDIATE(instruction))
            RAISE_EXCEPTION(EXCEPTION_CODE_TRAP, {});
          break;

        case TOKEN_TGE_OPCODE:
          if (general_purpose_registers[SOURCE_REGISTER(instruction)] >= general_purpose_registers[TARGET_REGISTER(instruction)])
            RAISE_EXCEPTION(EXCEPTION_CODE_TRAP, {});
          break;

        case TOKEN_TGEI_OPCODE:
          if (general_purpose_registers[SOURCE_REGISTER(instruction)] >= IMMEDIATE(instruction))
            RAISE_EXCEPTION(EXCEPTION_CODE_TRAP, {});
          break;

        case TOKEN_TGEIU_OPCODE:
          if ((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] >= (unsigned_register_word)IMMEDIATE(instruction))
            RAISE_EXCEPTION(EXCEPTION_CODE_TRAP, {});
          break;

        case TOKEN_TGEU_OPCODE:
          if ((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] >=
              (unsigned_register_word)general_purpose_registers[TARGET_REGISTER(instruction)])
            RAISE_EXCEPTION(EXCEPTION_CODE_TRAP, {});
          break;

        case TOKEN_TLBP_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_RESERVED_INSTRUCTION, {}); /* TLB not implemented */
          break;

        case TOKEN_TLBR_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_RESERVED_INSTRUCTION, {}); /* TLB not implemented */
          break;

        case TOKEN_TLBWI_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_RESERVED_INSTRUCTION, {}); /* TLB not implemented */
          break;

        case TOKEN_TLBWR_OPCODE:
          RAISE_EXCEPTION(EXCEPTION_CODE_RESERVED_INSTRUCTION, {}); /* TLB not implemented */
          break;

        case TOKEN_TLT_OPCODE:
          if (general_purpose_registers[SOURCE_REGISTER(instruction)] < general_purpose_registers[TARGET_REGISTER(instruction)])
            RAISE_EXCEPTION(EXCEPTION_CODE_TRAP, {});
          break;

        case TOKEN_TLTI_OPCODE:
          if (general_purpose_registers[SOURCE_REGISTER(instruction)] < IMMEDIATE(instruction))
            RAISE_EXCEPTION(EXCEPTION_CODE_TRAP, {});
          break;

        case TOKEN_TLTIU_OPCODE:
          if ((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] < (unsigned_register_word)IMMEDIATE(instruction))
            RAISE_EXCEPTION(EXCEPTION_CODE_TRAP, {});
          break;

        case TOKEN_TLTU_OPCODE:
          if ((unsigned_register_word)general_purpose_registers[SOURCE_REGISTER(instruction)] <
              (unsigned_register_word)general_purpose_registers[TARGET_REGISTER(instruction)])
            RAISE_EXCEPTION(EXCEPTION_CODE_TRAP, {});
          break;

        case TOKEN_TNE_OPCODE:
          if (general_purpose_registers[SOURCE_REGISTER(instruction)] != general_purpose_registers[TARGET_REGISTER(instruction)])
            RAISE_EXCEPTION(EXCEPTION_CODE_TRAP, {});
          break;

        case TOKEN_TNEI_OPCODE:
          if (general_purpose_registers[SOURCE_REGISTER(instruction)] != IMMEDIATE(instruction))
            RAISE_EXCEPTION(EXCEPTION_CODE_TRAP, {});
          break;

        case TOKEN_XOR_OPCODE:
          general_purpose_registers[DESTINATION_REGISTER(instruction)] = general_purpose_registers[SOURCE_REGISTER(instruction)] ^ general_purpose_registers[TARGET_REGISTER(instruction)];
          break;

        case TOKEN_XORI_OPCODE:
          general_purpose_registers[TARGET_REGISTER(instruction)] =
              general_purpose_registers[SOURCE_REGISTER(instruction)] ^ (0xffff & IMMEDIATE(instruction));
          break;

          /* FPA Operations */

        case TOKEN_ABS_S_OPCODE:
          SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction), fabs(FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction))));
          break;

        case TOKEN_ABS_D_OPCODE:
          SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction), fabs(FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction))));
          break;

        case TOKEN_ADD_S_OPCODE:
          SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction),
                    FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction)) + FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_TARGET(instruction)));
          /* Should trap on inexact/overflow/underflow */
          break;

        case TOKEN_ADD_D_OPCODE:
          SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction),
                    FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction)) + FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_TARGET(instruction)));
          /* Should trap on inexact/overflow/underflow */
          break;

        case TOKEN_BC1F_OPCODE:
        case TOKEN_BC1FL_OPCODE:
        case TOKEN_BC1T_OPCODE:
        case TOKEN_BC1TL_OPCODE: {
          const int condition_code = CONDITION_CODE(instruction);
          const int nullify_delay = NULLIFY_DELAY(instruction); /* 1 => nullify */
          const int true_false = TRUE_FALSE(instruction); /* 0 => BC1F, 1 => BC1T */
          BRANCH_INSTRUCTION(floating_point_condition_code(condition_code) == true_false, PROGRAM_COUNTER + (memory_address)BRANCH_OFFSET(instruction),
                      nullify_delay);
          break;
        }

        case TOKEN_C_F_S_OPCODE:
        case TOKEN_C_UN_S_OPCODE:
        case TOKEN_C_EQ_S_OPCODE:
        case TOKEN_C_UEQ_S_OPCODE:
        case TOKEN_C_OLT_S_OPCODE:
        case TOKEN_C_OLE_S_OPCODE:
        case TOKEN_C_ULT_S_OPCODE:
        case TOKEN_C_ULE_S_OPCODE:
        case TOKEN_C_SF_S_OPCODE:
        case TOKEN_C_NGLE_S_OPCODE:
        case TOKEN_C_SEQ_S_OPCODE:
        case TOKEN_C_NGL_S_OPCODE:
        case TOKEN_C_LT_S_OPCODE:
        case TOKEN_C_NGE_S_OPCODE:
        case TOKEN_C_LE_S_OPCODE:
        case TOKEN_C_NGT_S_OPCODE: {
          const float v1 = FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction)), v2 = FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_TARGET(instruction));
          const double double_value1 = v1, double_value2 = v2;
          const int condition = CONDITION(instruction);
          const int condition_code = FLOATING_POINT_CONDITION_CODE(instruction);

          if (isnan(double_value1) || isnan(double_value2)) {
            if ((uint32_t)condition & CONDITION_INVALID) {
              RAISE_EXCEPTION(EXCEPTION_CODE_FLOATING_POINT_EXCEPTION, break);
            }
            set_floating_point_unit_condition_code(condition, condition_code, 0, 0, 1);
          } else {
            set_floating_point_unit_condition_code(condition, condition_code, v1 < v2, v1 == v2, 0);
          }
        } break;

        case TOKEN_C_F_D_OPCODE:
        case TOKEN_C_UN_D_OPCODE:
        case TOKEN_C_EQ_D_OPCODE:
        case TOKEN_C_UEQ_D_OPCODE:
        case TOKEN_C_OLT_D_OPCODE:
        case TOKEN_C_OLE_D_OPCODE:
        case TOKEN_C_ULT_D_OPCODE:
        case TOKEN_C_ULE_D_OPCODE:
        case TOKEN_C_SF_D_OPCODE:
        case TOKEN_C_NGLE_D_OPCODE:
        case TOKEN_C_SEQ_D_OPCODE:
        case TOKEN_C_NGL_D_OPCODE:
        case TOKEN_C_LT_D_OPCODE:
        case TOKEN_C_NGE_D_OPCODE:
        case TOKEN_C_LE_D_OPCODE:
        case TOKEN_C_NGT_D_OPCODE: {
          const double v1 = FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction)), v2 = FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_TARGET(instruction));
          const int condition = CONDITION(instruction);
          const int condition_code = FLOATING_POINT_CONDITION_CODE(instruction);

          if (isnan(v1) || isnan(v2)) {
            if ((uint32_t)condition & CONDITION_INVALID) {
              RAISE_EXCEPTION(EXCEPTION_CODE_FLOATING_POINT_EXCEPTION, break);
            }
            set_floating_point_unit_condition_code(condition, condition_code, 0, 0, 1);
          } else {
            set_floating_point_unit_condition_code(condition, condition_code, v1 < v2, v1 == v2, 0);
          }
        } break;

        case TOKEN_CFC1_OPCODE:
          general_purpose_registers[TARGET_REGISTER(instruction)] = floating_point_control_registers[FLOATING_POINT_SOURCE(instruction)];
          break;

        case TOKEN_CTC1_OPCODE:
          floating_point_control_registers[FLOATING_POINT_SOURCE(instruction)] = general_purpose_registers[TARGET_REGISTER(instruction)];

          if (FLOATING_POINT_IMPLEMENTATION_REGISTER_NUMBER == FLOATING_POINT_SOURCE(instruction)) {
            /* Read only register */
          } else if (FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER == FLOATING_POINT_SOURCE(instruction)) {
            if (((unsigned_register_word)general_purpose_registers[TARGET_REGISTER(instruction)] & ~FLOATING_POINT_CONTROL_STATUS_MASK) != 0)
              /* Trying to set unsupported mode */
              RAISE_EXCEPTION(EXCEPTION_CODE_FLOATING_POINT_EXCEPTION, {});
          } else {
            /* The other FP control registers take any value. */
          }
          break;

        case TOKEN_CEIL_W_D_OPCODE: {
          const double value = FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_WORD(FLOATING_POINT_DESTINATION(instruction), (int32_t)ceil(value));
          break;
        }

        case TOKEN_CEIL_W_S_OPCODE: {
          const double value = (double)FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_WORD(FLOATING_POINT_DESTINATION(instruction), (int32_t)ceil(value));
          break;
        }

        case TOKEN_CVT_D_S_OPCODE: {
          const double value = FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction), value);
          break;
        }

        case TOKEN_CVT_D_W_OPCODE: {
          const double value = (double)FLOATING_POINT_REGISTER_WORD(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction), value);
          break;
        }

        case TOKEN_CVT_S_D_OPCODE: {
          const float value = (float)FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction), value);
          break;
        }

        case TOKEN_CVT_S_W_OPCODE: {
          const float value = (float)FLOATING_POINT_REGISTER_WORD(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction), value);
          break;
        }

        case TOKEN_CVT_W_D_OPCODE: {
          const int value = (int32_t)FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_WORD(FLOATING_POINT_DESTINATION(instruction), value);
          break;
        }

        case TOKEN_CVT_W_S_OPCODE: {
          const int value = (int32_t)FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_WORD(FLOATING_POINT_DESTINATION(instruction), value);
          break;
        }

        case TOKEN_DIV_S_OPCODE:
          SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction),
                    FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction)) / FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_TARGET(instruction)));
          break;

        case TOKEN_DIV_D_OPCODE:
          SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction),
                    FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction)) / FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_TARGET(instruction)));
          break;

        case TOKEN_FLOOR_W_D_OPCODE: {
          const double value = FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_WORD(FLOATING_POINT_DESTINATION(instruction), (int32_t)floor(value));
          break;
        }

        case TOKEN_FLOOR_W_S_OPCODE: {
          const double value = (double)FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_WORD(FLOATING_POINT_DESTINATION(instruction), (int32_t)floor(value));
          break;
        }

        case TOKEN_LDC1_OPCODE: {
          const memory_address address = (unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                (unsigned_register_word)IMMEDIATE_OFFSET(instruction);
          if ((address & 0x3) != 0)
            RAISE_EXCEPTION(EXCEPTION_CODE_ADDRESS_ERROR_LOAD, coprocessor_0_bad_virtual_address = (register_word)address);

          LOAD_INSTRUCTION((register_word*)&FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_TARGET(instruction)), memory_read_word(address),
                    (register_word)0xffffffff);
          LOAD_INSTRUCTION((register_word*)&FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_TARGET(instruction) + 1),
                    memory_read_word(address + sizeof(memory_word)),
                    (register_word)0xffffffff);
          break;
        }

        case TOKEN_LWC1_OPCODE:
          LOAD_INSTRUCTION((register_word*)&FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_TARGET(instruction)),
                    memory_read_word((unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                  (unsigned_register_word)IMMEDIATE_OFFSET(instruction)),
                    (register_word)0xffffffff);
          break;

        case TOKEN_MFC1_OPCODE: {
          float value = FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction));
          register_word bits; /* the float's bit pattern, not its value */
          memcpy(&bits, &value, sizeof bits);

          general_purpose_registers[TARGET_REGISTER(instruction)] = bits;
          break;
        }

        case TOKEN_MOV_S_OPCODE:
          SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction)));
          break;

        case TOKEN_MOV_D_OPCODE:
          SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction)));
          break;

        case TOKEN_MOVF_OPCODE: {
          const int condition_code = CONDITION_CODE(instruction);
          if (floating_point_condition_code(condition_code) == 0) general_purpose_registers[DESTINATION_REGISTER(instruction)] = general_purpose_registers[SOURCE_REGISTER(instruction)];
          break;
        }

        case TOKEN_MOVF_D_OPCODE: {
          const int condition_code = CONDITION_CODE(instruction);
          if (floating_point_condition_code(condition_code) == 0) SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction)));
          break;
        }

        case TOKEN_MOVF_S_OPCODE: {
          const int condition_code = CONDITION_CODE(instruction);
          if (floating_point_condition_code(condition_code) == 0) SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction)));
          break;
        }

        case TOKEN_MOVN_D_OPCODE: {
          if (general_purpose_registers[TARGET_REGISTER(instruction)] != 0)
            SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction)));
          break;
        }

        case TOKEN_MOVN_S_OPCODE: {
          if (general_purpose_registers[TARGET_REGISTER(instruction)] != 0)
            SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction)));
          break;
        }

        case TOKEN_MOVT_OPCODE: {
          const int condition_code = CONDITION_CODE(instruction);
          if (floating_point_condition_code(condition_code) != 0) general_purpose_registers[DESTINATION_REGISTER(instruction)] = general_purpose_registers[SOURCE_REGISTER(instruction)];
          break;
        }

        case TOKEN_MOVT_D_OPCODE: {
          const int condition_code = CONDITION_CODE(instruction);
          if (floating_point_condition_code(condition_code) != 0) SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction)));
          break;
        }

        case TOKEN_MOVT_S_OPCODE: {
          const int condition_code = CONDITION_CODE(instruction);
          if (floating_point_condition_code(condition_code) != 0) SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction)));
          break;
        }

        case TOKEN_MOVZ_D_OPCODE: {
          if (general_purpose_registers[TARGET_REGISTER(instruction)] == 0)
            SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction)));
          break;
        }

        case TOKEN_MOVZ_S_OPCODE: {
          if (general_purpose_registers[TARGET_REGISTER(instruction)] == 0)
            SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction), FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction)));
          break;
        }

        case TOKEN_MTC1_OPCODE: {
          register_word word = general_purpose_registers[TARGET_REGISTER(instruction)];
          float value; /* the word's bit pattern, not its value */
          memcpy(&value, &word, sizeof value);

          SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction), value);
          break;
        }

        case TOKEN_MUL_S_OPCODE:
          SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction),
                    FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction)) * FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_TARGET(instruction)));
          break;

        case TOKEN_MUL_D_OPCODE:
          SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction),
                    FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction)) * FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_TARGET(instruction)));
          break;

        case TOKEN_NEG_S_OPCODE:
          SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction), -FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction)));
          break;

        case TOKEN_NEG_D_OPCODE:
          SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction), -FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction)));
          break;

        case TOKEN_ROUND_W_D_OPCODE: {
          const double value = FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_WORD(FLOATING_POINT_DESTINATION(instruction),
                    (int32_t)(value + 0.5)); /* Casting truncates */
          break;
        }

        case TOKEN_ROUND_W_S_OPCODE: {
          const double value = (double)FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_WORD(FLOATING_POINT_DESTINATION(instruction),
                    (int32_t)(value + 0.5)); /* Casting truncates */
          break;
        }

        case TOKEN_SDC1_OPCODE: {
          double value = FLOATING_POINT_REGISTER_DOUBLE(TARGET_REGISTER(instruction));
          register_word words[2]; /* the double's two words, in host order */
          memcpy(words, &value, sizeof words);
          const memory_address address = (unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                                (unsigned_register_word)IMMEDIATE_OFFSET(instruction);
          if ((address & 0x3) != 0)
            RAISE_EXCEPTION(EXCEPTION_CODE_ADDRESS_ERROR_LOAD, coprocessor_0_bad_virtual_address = (register_word)address);

          memory_write_word(address, words[0]);
          memory_write_word(address + sizeof(memory_word), words[1]);
          break;
        }

        case TOKEN_SQRT_D_OPCODE:
          SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction), sqrt(FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction))));
          break;

        case TOKEN_SQRT_S_OPCODE:
          SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction), sqrt(FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction))));
          break;

        case TOKEN_SUB_S_OPCODE:
          SET_FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_DESTINATION(instruction),
                    FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction)) - FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_TARGET(instruction)));
          break;

        case TOKEN_SUB_D_OPCODE:
          SET_FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_DESTINATION(instruction),
                    FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction)) - FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_TARGET(instruction)));
          break;

        case TOKEN_SWC1_OPCODE: {
          float value = FLOATING_POINT_REGISTER_SINGLE(TARGET_REGISTER(instruction));
          register_word bits; /* the float's bit pattern, not its value */
          memcpy(&bits, &value, sizeof bits);

          memory_write_word((unsigned_register_word)general_purpose_registers[BASE(instruction)] +
                             (unsigned_register_word)IMMEDIATE_OFFSET(instruction),
                         bits);
          break;
        }

        case TOKEN_TRUNC_W_D_OPCODE: {
          const double value = FLOATING_POINT_REGISTER_DOUBLE(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_WORD(FLOATING_POINT_DESTINATION(instruction), (int32_t)value); /* Casting truncates */
          break;
        }

        case TOKEN_TRUNC_W_S_OPCODE: {
          const double value = (double)FLOATING_POINT_REGISTER_SINGLE(FLOATING_POINT_SOURCE(instruction));

          SET_FLOATING_POINT_REGISTER_WORD(FLOATING_POINT_DESTINATION(instruction), (int32_t)value); /* Casting truncates */
          break;
        }

        default:
          fatal_error("Unknown instruction type: %d\n", OPCODE(instruction));
          break;
      }

      /* After instruction executes: */
      PROGRAM_COUNTER += BYTES_PER_WORD;

      explain_after(instruction);

      if (display) print_instruction(PROGRAM_COUNTER);

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

static void bump_coprocessor_0_timer(void) {
  coprocessor_0_count += 1;
  if (coprocessor_0_count == coprocessor_0_compare) {
    RAISE_INTERRUPT(7);
  }
}

static void start_coprocessor_0_timer(void) {
  /* Read the timer with getitimer rather than handling SIGALRM, since signals
     interrupt I/O calls (read, etc.) and make user interaction with SPIM work
     very poorly. Since speed isn't an important aspect of SPIM, polling isn't
     a big deal. */
  if (SIG_ERR == signal(SIGALRM, SIG_IGN)) {
    perror("signal failed");
  } else {
    struct itimerval time = {};
    if (-1 == getitimer(ITIMER_REAL, &time)) {
      perror("getitmer failed");
    }
    if (time.it_value.tv_usec == 0 && time.it_value.tv_sec == 0) {
      /* Timer is expired or has not been started.
         Start a non-periodic timer for TIMER_TICK_MS microseconds. */
      time = (struct itimerval){.it_value.tv_usec = TIMER_TICK_MS * 1000};
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

static void unsigned_multiply(register_word v1, register_word v2) {
  const unsigned_register_word a = (unsigned_register_word)((v1 >> 16) & 0xffff);
  const unsigned_register_word b = v1 & 0xffff;
  const unsigned_register_word c = (unsigned_register_word)((v2 >> 16) & 0xffff);
  const unsigned_register_word d = v2 & 0xffff;

  const unsigned_register_word bd = b * d;
  const unsigned_register_word ad = a * d;
  const unsigned_register_word cb = c * b;
  const unsigned_register_word ac = a * c;

  const unsigned_register_word mid = ad + cb;
  unsigned_register_word carry_mid = 0;
  if (mid < ad || mid < cb) /* Arithmetic overflow or carry-out */
    carry_mid = 1;

  const unsigned_register_word mid2 = mid + ((bd >> 16) & 0xffff);
  if (mid2 < mid || mid2 < ((bd >> 16) & 0xffff))
    /* Arithmetic overflow or carry-out */
    carry_mid += 1;

  MULTIPLY_DIVIDE_LOW = (register_word)((bd & 0xffff) | ((mid2 & 0xffff) << 16));
  MULTIPLY_DIVIDE_HIGH = (register_word)(ac + (carry_mid << 16) + ((mid2 >> 16) & 0xffff));
}

static void signed_multiply(register_word v1, register_word v2) {
  bool neg_sign = false;

  /* Negate as unsigned: -v is UB when v == INT_MIN.  The two's-complement
     bit pattern (which unsigned_multiply consumes as a magnitude) is identical.
   */
  if (v1 < 0) {
    v1 = (register_word)(-(unsigned_register_word)v1);
    neg_sign = true;
  }
  if (v2 < 0) {
    v2 = (register_word)(-(unsigned_register_word)v2);
    neg_sign = !neg_sign;
  }

  unsigned_multiply(v1, v2);
  if (neg_sign) {
    /* Two's-complement negation of the 64-bit HI:LO, in unsigned arithmetic
       (LO + 1 overflows int32_t when ~LO is INT32_MAX). */
    MULTIPLY_DIVIDE_LOW = (register_word)(~(unsigned_register_word)MULTIPLY_DIVIDE_LOW + 1u);
    MULTIPLY_DIVIDE_HIGH = (register_word)(~(unsigned_register_word)MULTIPLY_DIVIDE_HIGH + (MULTIPLY_DIVIDE_LOW == 0 ? 1u : 0u));
  }
}

static void set_floating_point_unit_condition_code(int condition, int condition_code, int less, int equal, int unordered) {
  int result = 0;

  if ((uint32_t)condition & CONDITION_LESS_THAN) result |= less;
  if ((uint32_t)condition & CONDITION_EQUAL) result |= equal;
  if ((uint32_t)condition & CONDITION_UNORDERED) result |= unordered;

  set_floating_point_condition_code(condition_code, result);
}

void raise_exception(mips_exception_code exception_code) {
  if (EXCEPTION_CODE_INTERRUPT != exception_code ||
      (((uint32_t)coprocessor_0_status &
        COPROCESSOR_0_STATUS_INTERRUPT_ENABLE) /* Allow interrupt if IE and !EXL */
       && !((uint32_t)coprocessor_0_status & COPROCESSOR_0_STATUS_EXCEPTION_LEVEL))) {
    /* Ignore interrupt exception when interrupts disabled.  */
    exception_occurred = true;
    /* Record the FIRST "bad" exception — anything other than a normal-flow
       interrupt (0), syscall (8), or breakpoint (9).  Used by main() to
       set a non-zero shell exit status when the user program took a fault.
       Only the first wins because spim's default handler advances past the
       faulting instruction and continues; without the latch, a misbehaving
       loop would overwrite the status repeatedly. */
    if (first_bad_exception == -1 && exception_code != EXCEPTION_CODE_INTERRUPT &&
        exception_code != EXCEPTION_CODE_SYSTEM_CALL && exception_code != EXCEPTION_CODE_BREAKPOINT) {
      first_bad_exception = exception_code;
    }
    if (running_in_delay_slot) {
      /* In delay slot */
      if (((uint32_t)coprocessor_0_status & COPROCESSOR_0_STATUS_EXCEPTION_LEVEL) == 0) {
        /* Branch's addr */
        coprocessor_0_exception_program_counter = (register_word)ROUND_DOWN(PROGRAM_COUNTER - BYTES_PER_WORD, BYTES_PER_WORD);
        /* Set BD bit to record that instruction is in delay slot */
        coprocessor_0_cause = (register_word)((unsigned_register_word)coprocessor_0_cause | COPROCESSOR_0_CAUSE_BRANCH_DELAY);
      }
    } else {
      /* Not in delay slot */
      if (((uint32_t)coprocessor_0_status & COPROCESSOR_0_STATUS_EXCEPTION_LEVEL) == 0) {
        /* Faulting instruction's address */
        coprocessor_0_exception_program_counter = (register_word)ROUND_DOWN(PROGRAM_COUNTER, BYTES_PER_WORD);
      }
    }
    /* ToDo: set CE field of Cause register to coprocessor causing exception */

    /* Record cause of exception */
    coprocessor_0_cause = (register_word)(((uint32_t)coprocessor_0_cause & ~COPROCESSOR_0_CAUSE_EXCEPTION_CODE) |
                           (uint32_t)(exception_code << 2));

    /* Turn on EXL bit to prevent subsequent interrupts from affecting EPC */
    coprocessor_0_status = (register_word)((unsigned_register_word)coprocessor_0_status | COPROCESSOR_0_STATUS_EXCEPTION_LEVEL);

#ifdef MIPS1
    coprocessor_0_status = (coprocessor_0_status & 0xffffffc0) | ((coprocessor_0_status & 0xf) << 2);
#endif
  }
}
