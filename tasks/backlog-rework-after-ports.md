# Rework the backlog after the ports: retire or trim stale tasks, add ports steps, rewrite tasks/README.md ordering

**Status:** proposed — needs go-ahead
**Priority:** 5
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §9.1, §9.2, §9.3, §9.4, §1.1 F15

## BLUF

Several open tasks predate the ports and the generator and no longer read true. Done means:
- `tasks/codebase-cleanup-plan.md` holds only its open items, or is archived;
- `tasks/minimal-nested-image.md` is rewritten against `MINIMAL_IMAGE`, or retired;
- each behavior-changing task has a "follow in every port" step, and a rating to match;
- `tasks/engine-ports-teaching-mode.md` uses `Depends on` instead of `blocked`;
- `tasks/README.md`'s ordering section is rebuilt from the current backlog.

## Context

- `tasks/codebase-cleanup-plan.md`:
  - done: A1–A4, A6, A10, B2, C4 and C5 (a grep each: `parse_br_imm32`, `parser_error_occurred`,
    `README.rtf`, `bldInstall`, `wsock32`, `_AIX`, `str_copy`, the include guards);
  - F4 is about the retired `opcodes.h`, and G2 is marked superseded;
  - its verification gate leaves out the ports and `make image`, and it has no Status or BLUF.
- `tasks/minimal-nested-image.md:3-24,145` builds on the `NESTED_PODMAN`-keyed idiom. The fleet
  replaced that with the opt-in `MINIMAL_IMAGE` on 2026-09-27 (the minimal-nested-images.md
  reference doc from github.com/billsix/runClaudeInContainer). It also cites `opcodes.h`.
- These change C behavior with no ports step (`CLAUDE.md`: "A C behavior change must be followed in
  every port"):
  - `ast-column-tracking` (`-print-ast` output, compared by the port suites);
  - `explain-mode-correctness-bugs` (the REPL `step` fix and `-show-expansion`);
  - `explain-levels-redesign` (a new `-encoding` flag and a REPL `explain` command);
  - `timing-model`;
  - `software-alu`.
- `tasks/engine-ports-teaching-mode.md:3-5` is `blocked` on two in-repo tasks. The task-doc
  convention is Priority plus `Depends on`.
- `tasks/examples-c-debug-build-via-meson.md:3` says "ready to implement" with three open questions;
  `string-equality-audit` is done and waits on an answer.
- `tasks/README.md:111-175`, "Ordering & dependencies (reviewed 2026-07-07)", is mostly about
  archived work. Its ChangeLog step is obsolete, and its status table doesn't match the statuses in
  use. The record's §9 has the backlog as it reads today.

## Plan

1. Trim or archive `tasks/codebase-cleanup-plan.md`; rewrite or retire `tasks/minimal-nested-image.md` (question 1).
2. Add a ports step to each of the five tasks, and re-rate their Difficulty.
3. Fix the status lines (`engine-ports-teaching-mode`, `examples-c-debug-build-via-meson`).
4. Rebuild `tasks/README.md`'s ordering section from the record's backlog table, and drop the ChangeLog
   step.

## Open questions

1. `tasks/minimal-nested-image.md`: (a) rewrite it against `MINIMAL_IMAGE`, or (b) retire it, since nested
   builds are now full by default? Recommend (b) unless a lean export image is still wanted.
2. `tasks/codebase-cleanup-plan.md`: (a) trim it to the open items, or (b) archive it and file the few open
   items as their own tasks? Recommend (a).
