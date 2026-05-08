/* Teaching mode: narrate each instruction before/after execution. */

#ifndef EXPLAIN_H
#define EXPLAIN_H

#include <stdbool.h>
#include "spim.h"
#include "inst.h"

extern bool explain_mode;

/* Called before the dispatch switch in run_spim. Snapshots register state and
   prints a description of the upcoming instruction. */
void explain_before(instruction* inst, mem_addr addr);

/* Called after the dispatch switch. Prints a diff of register/PC state vs
   the snapshot taken in explain_before. */
void explain_after(instruction* inst);

#endif
