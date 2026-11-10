# C23 modernization pass 3 — make the core as clean as C23 allows before any port

**Status:** in progress — after the audit
**Priority:** 3
**Difficulty:** 6
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Branch:** `c23-pass3-and-port-study`
**Source:** tasks/review/2026/11/10/repo-review.md §4.1, §4.2, §4.3, §4.4, §4.5, §4.6, §4.7, §4.8 F1

## BLUF

Bring `src/` + `include/` to the cleanest C23 the code can be, so the later language port starts
from tidy C. The maintainer named **struct initialization** first (designated initializers,
compound literals, `= {}` empty initialization, initialize-at-declaration instead of
declare-then-assign-each-field); the audit supplies the rest of the worklist. Done means: the
worklist from the audit record's C23 section is applied (or each item explicitly opted out with a
reason), the image gate (`make image`, which runs the regression + examples suites and the
UBSan/ASan builds) is green, and `BUILD_TREE_SITTER=1` is on if `opcodes.h` is touched.

## Context (read first)

- Standard: the `c-coding-standard.md` and `c-modernization-sweep.md` reference docs from
  github.com/billsix/runClaudeInContainer (the sweep doc lists where idioms must NOT be applied:
  setjmp/longjmp locals, wire-format zero-fill, deliberate teaching bugs).
- Scope is `src/` + `include/` only. `examples/` and `pgu/` C is deliberately not modernized
  (`tasks/reference/example-c-teaching-style.md`).
- Prior passes: `tasks/archive/*/c23-modernization.md`, `.../c23-modernization-pass2.md`.
- Overlap: `tasks/codebase-cleanup-plan.md` — its C-standard items are folded in here (Decisions).

## Plan

1. Take the audit's C23 worklist; group by idiom (struct init, `constexpr`, `nullptr`, `bool`,
   `[[nodiscard]]`/`[[maybe_unused]]`, `typeof`, enums with fixed underlying type, `static_assert`,
   `unreachable()`, …).
2. Apply one idiom group per commit; gate after each (`make image` nested; run the gate's own
   `podman run` line if the target would rebuild).
3. Opt-outs written in-code with the reason; reported in the done-record.

## Decisions

- The C-standard items of `tasks/codebase-cleanup-plan.md` move into this task; its non-C23 refactors stay there (William Emerison Six <billsix@gmail.com>, 2026-11-10).
