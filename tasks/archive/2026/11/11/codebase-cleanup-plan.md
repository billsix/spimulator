# Finished the open items of the codebase cleanup plan

**Completed:** 2026-11-11
**Source:** the 2026-05 cleanup plan of the simulator core; trimmed to its open items 2026-11-11 by
`tasks/archive/2026/11/11/backlog-rework-after-ports.md`.

## Summary

The original plan's other items were done before the trim (A1–A6, A8–A10, B1, B2, C3–C5, E1, E2,
E4), and C2 was dropped. The full original plan is in git history. What was left:

- **A7, an unchecked `realloc`:** the REPL `args` command grew `owned_strs` with a bare `realloc` and
  built `owned_argv` with an unchecked `malloc`. A new `xrealloc` beside `xmalloc`
  (`src/spim-utils.c`) makes out of memory fatal with the same message. It also replaced the
  unchecked `realloc`s in `make_memory` and the symbol-table string buffer. `expand_data` and
  `expand_k_data` kept their own checks and messages, which the ports reproduce. There is no test,
  because the failure needs an allocation to fail.
- **B3, the `_inst` suffix:** dropped from the whole emitter family in C (`r_type`, `i_type`,
  `i_type_free`, `i_type_full_word`, `j_type`, `make_r_type`, `r_co_type`, `r_cond_type`,
  `r_sh_type`) and in each port's counterparts (Haskell `rType`…, Python `_make_r_type`…, Rust
  `Machine::r_type`…, beside Rust's existing `Instruction::r_type` constructor). The new names were
  checked for collisions first. The rename was done by a codemod that rewrote whole words in
  tracked sources only, was idempotent (a second run changed nothing), and was removed with this
  archive.
- **C1, splitting `include/spim.h`:** declined (decision 2).
- **E3, exception-path tests:** `stack_overflow` (recursion past `-lstack 65536`) and
  `kernel_round_trip` (`teq` traps into the handler, `eret` returns, EXL is clear before and after,
  exit 141), both in the regression suite and the port corpus. Writing the first exposed a bug in
  all four engines: a run-time error that stopped a batch run exited 0. It now exits 1, as a
  run-time error while loading already did, unless the program set its own status.
  `Documentation/spimulator.1` (EXIT STATUS) says so.

Gates: `meson test` 348/348 (gcc), clang regression 53/53.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. B3: drop the `_inst` suffix (the plan's recommendation).
2. C1: declined; the header split is the lowest-value item and not worth the churn.
3. A7 and E3 have no open question and are done with the rest.
