# Codebase cleanup: the open items

**Status:** in progress (go-ahead 2026-11-11)
**Priority:** 7
**Difficulty:** 3

## BLUF

The open items left from the 2026-05 cleanup plan of the simulator core (`src/` + `include/`): one
unchecked `realloc`, a naming question, one header split, and two missing exception tests. Done
means each item below is either done or explicitly declined. Every item is small and stands alone.

## Context

- Trimmed 2026-11-11 (`tasks/archive/2026/11/11/backlog-rework-after-ports.md`, the maintainer's decision 2): the
  original plan's done items were removed. Done, each checked by a grep that day: A1–A6, A8–A10
  (dead code, include guards, the README build claim, `README.rtf`, the duplicate `extern`s, the
  ChangeLog note, `bldInstall/`), B1, B2 (`str_copy`), C3 (the SPDX headers and `LICENSE`), C4
  (`wsock32`), C5 (`_AIX` and friends), E1, E2, E4. C2 was dropped: `eval_imm_expr` is called from
  four files, so its declaration belongs in the header. The full original plan is in git history.
- **A C behavior change must be followed in every port** (`CLAUDE.md`); E3's tests are new
  regression cases, so each port suite runs them too.

## Open items

### A7. Check the bare `realloc` of `owned_strs` (`src/spim.c`, the `owned_strs = realloc(` site)

The `label_names_cache` one was fixed. The `owned_strs` growth in the argument handling assigns
`realloc`'s result straight back, so an allocation failure loses the old pointer and dereferences
null. Use the file's existing fatal-on-null convention.

### B3. Decide whether `*_inst` is a suffix convention or noise

`r_type_inst`, `i_type_inst`, `j_type_inst` (`include/instruction.h`) end in `_inst`; `store_word`,
`store_half`, `store_byte`, which also emit instructions, don't. The plan's recommendation: drop the
suffix. A rename here touches the ports' matching names too (they mirror the C names).

### C1. Split `include/spim.h`

It bundles types, sizing constants, declarations, exported globals and platform shims (`MIPS1`,
`SPIM_BIGENDIAN`, `MIN`/`MAX`). A split into types / sizes / declarations would shrink what a TU
that needs only `mem_addr` pulls in. Lowest value of the four; decline if it isn't worth the churn.

### E3. Exception-path tests: stack overflow and kernel-mode transitions

Divide-by-zero has `tests/tt.divide_by_zero.s` and `break` is exercised by `tests/tt.core.s`. A stack
overflow (the stack segment's growth limit) and a user→kernel→user transition have no test of their
own. Add each as a `tests/tt.*.s` case with a pinned golden, in `meson.build`'s `regression_tests`
and `port/harness/corpus.txt`.

## Not pursued (decisions kept from the original plan)

- **Tier F, structural refactors** (context structs for the file statics, an opaque memory type,
  splitting `run.c`'s switch): considered and not pursued 2026-05-20; the cost is ~150+ call sites
  for testability the regression and port suites already give.
- **G1** keep `setjmp`/`longjmp` for the SIGINT escape. **G3** keep manual `xmalloc`/`free`. **G4**
  keep bitfield overlays. **G5** keep the run-mode globals (`bare_machine`, `delayed_branches`, …)
  global. **G6** no `[[deprecated]]` without an external API.
- **G2** (keep the X-macro) was superseded 2026-11-11 by the maintainer's choice of one data file and
  a generator (`tasks/archive/2026/11/11/opcodes-x-macro-redesign.md`).

## Plan

1. Per item: make the change, then gate with both local builds and `meson test` (regression,
   examples, port-python, port-haskell, port-rust), then `make image`.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. B3: drop the `_inst` suffix (the plan's recommendation).
2. C1: declined; the header split is the lowest-value item and not worth the churn.
3. A7 and E3 have no open question and are done with the rest.
