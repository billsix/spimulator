/* Teaching mode: narrate each instruction before/after execution. */

#ifndef EXPLAIN_H
#define EXPLAIN_H

#include <stdbool.h>
#include "spim.h"
#include "inst.h"

/* Teaching detail level:
     0 — off (no narration; baseline spim behavior)
     1 — semantic: what the instruction does + after-step diff
     2 — + interactive: Inputs/Will-write/Try-it-yourself + syscall details
     3 — + machine encoding: bit-layout diagram + mnemonic decoding
   Level 3 is the historical -explain behavior. */
extern int explain_level;
#define explain_mode (explain_level > 0)

/* Called before the dispatch switch in run_spim. Snapshots register state and
   prints a description of the upcoming instruction. */
void explain_before(instruction* inst, mem_addr addr);

/* Called after the dispatch switch. Prints a diff of register/PC state vs
   the snapshot taken in explain_before. */
void explain_after(instruction* inst);

/* Emit the per-step header (legend on first call, then hr separator,
   "Stepped at PC = X:", labeled machine-view line, optional source
   line) for the instruction at `pc`. Used by explain_after at the top
   of its body, and by the default step display in run.c so the
   no-explain step output has the same shape as the explain-mode
   header. Level-independent — `inst` is the instruction at `pc`. */
void explain_print_step_header(mem_addr pc, instruction* inst);

/* Tab-completion candidates for the most recent instruction's "Try it
   yourself" hints. The list is repopulated each time explain_before runs
   (and only at L2+, since L1 doesn't show the Try-it-yourself block —
   surfacing hidden suggestions via Tab would undermine the level
   distinction). Returned pointers are owned by explain.c and remain valid
   until the next explain_before / explain_clear_suggestions call. */
size_t explain_suggestion_count(void);
const char* explain_suggestion(size_t i);
void explain_clear_suggestions(void);

#endif
