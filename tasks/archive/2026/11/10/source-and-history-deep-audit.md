# Deep audit of the simulator core and its full git history (C23 plan + port-behavior inventory)

**Priority:** 3
**Difficulty:** 6
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-10
**Branch:** `c23-pass3-and-port-study`

## Outcome

A deep review (the repo-review-conventions.md reference doc from github.com/billsix/runClaudeInContainer)
of the simulator core (`src/`, `include/`, `tests/`, `meson.build`, `Documentation/spim.1`) and the
whole git history was written to the frozen record `tasks/review/2026/11/10/repo-review.md`, and the
project `CLAUDE.md` gained a `## Reviews` pointer to it. The record reads the core through two lenses:
the **C23 worklist** (§4), cited by `tasks/archive/2026/11/10/c23-modernization-pass3.md`, and the **port-behavior
inventory** (§1, §5–§7), cited by `tasks/archive/2026/11/10/engine-port-language-investigation.md`. It also lists 27
correctness bugs (§3) and 8 test-gate findings (§6), with follow-ups F1–F12 and two open questions.

## How it was done

- Seven fresh, read-only readers ran in parallel: four core subsystems read in full (scanner/parser/
  AST/data; run/memory/syscall/exceptions.s; spim.c REPL/driver + build files + man page;
  instruction codec/explain/opcodes.h), the tests corpus, and one history reader per era (upstream to
  SPIM 9.1.24; the maintainer era). About 1.29M subagent tokens in total.
- The main thread re-ran the checks behind every HIGH finding and every dead-code or
  byte-identity claim (marked MAIN-VERIFIED in the record), and reconciled against
  `tasks/archive/2026/11/11/codebase-cleanup-plan.md`, `tasks/reference/inherited-idiosyncrasies.md` and the pass-1/pass-2
  archive docs.

## Findings that changed the plan

- The upstream oracle is `6cb6a08` (SPIM 9.1.24 "final", 2023-08-04, merge-base with
  `github/upstream`), not the commit before the maintainer's first commit.
- Pass 2 had marked designated initializers "n/a" because the core zero-inits are dynamic; those
  dynamic `xmalloc`+field-assign constructors are exactly where `*p = (T){…}` applies, so pass 3 starts
  there (§4.1, ~110 sites).
- Two of upstream's bare-machine torture tests (`tt.alu.bare.s`, `tt.fpu.bare.s`) never run in the
  current suite, and the test named `bare` does not pass `-bare` (§6.1, §6.2).
- The flex/bison grammar is restorable from `6cb6a08:CPU/parser.y` (upstream) and
  `6bafa31^:src/parser.y` (last fork version) (§1.4).

## Decisions

- One combined deep audit with two lenses (C23, port behavior) and one record, instead of two audits (William Emerison Six <billsix@gmail.com>, 2026-11-10).
- The audit stayed read-only: the bugs it found are follow-ups F3–F8 in the record, not fixes in this
  task. Scope excluded `examples/`, `pgu/`, the task backlog and the archive (record "Coverage").
