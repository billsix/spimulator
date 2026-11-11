# Reworked the backlog after the ports

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Source:** tasks/review/2026/11/11/repo-review.md §9.1, §9.2, §9.3, §9.4, §1.1 F15

## Summary

- **`tasks/codebase-cleanup-plan.md`** was trimmed to its open items (decision 2, option (a)). Each
  item was checked with a grep first: A1–A6, A8–A10, B1, B2, C3, C4, C5, E1, E2 and E4 were done. C2
  was dropped, because `eval_imm_expr` is called from four files and belongs in the header. A7 was
  reworded: the `label_names_cache` realloc is checked now, but a bare `owned_strs = realloc(` in
  `src/spim.c` is not. Left open: A7, B3, C1, and E3, which now covers only stack overflow and
  kernel-mode transitions, since `tests/tt.core.s` exercises `break`. The "not pursued" decisions
  (Tier F, G1–G6) were kept as a short list. The plan now has a BLUF, a Status, a ports-aware gate and
  two open questions (B3, C1).
- **`tasks/minimal-nested-image.md`** was retired (decision 1, option (b)) and archived as
  `tasks/archive/2026/11/11/minimal-nested-image.md`.
- **Ports steps** were added to `ast-column-tracking`, `explain-mode-correctness-bugs`,
  `explain-levels-redesign`, `timing-model` and `software-alu`. The first four went up one
  Difficulty step. `software-alu` stayed at 8: its arithmetic must not change, and only what it
  makes visible follows in the ports.
- **Status lines:** `engine-ports-teaching-mode` uses `**Depends on:**` instead of `blocked`, and
  `examples-c-debug-build-via-meson` says it waits on three open questions.
- **`tasks/README.md`**: the status table now lists the statuses in use, the ordering section was
  rebuilt from the current backlog (reviewed 2026-11-11), and the ChangeLog step was replaced by the
  done-record and `git mv` steps.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. `minimal-nested-image.md` is retired (option (b)): nested builds are full by default.
2. `codebase-cleanup-plan.md` is trimmed to its open items (option (a)).
