/* SPIM S20 MIPS simulator.
   Execute SPIM instructions.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE in the project root for full text. */

/* Exported functions: */

#ifndef RUN_H
#define RUN_H

#include "spim.h"

[[nodiscard]] bool run_spim(memory_address initial_program_counter, int steps, bool display);

#endif
