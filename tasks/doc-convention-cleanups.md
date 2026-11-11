# Doc convention cleanups: stamps, links, history sections, missing BLUFs

**Status:** proposed — needs go-ahead
**Priority:** 8
**Difficulty:** 2
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §1.4, §1.5, §9.4, §10.6 F23

## BLUF

Convention fixes in open tasks and reference docs. Done means no bare first-name stamps, every link
repo-root-relative, the reference docs stating present truth (history in git), and each open task with
a Status line, a BLUF and Context.

## Context

- **Bare "Bill" stamps:** `tasks/string-equality-audit.md`, `tasks/mini-c-compiler.md`,
  `tasks/container-cross-env.md`, `tasks/multi-file-load.md`, `tasks/timing-model.md`, `tasks/pgu-build-matrix.md`, and
  `entrypoint/format.sh`. Use `William Emerison Six <billsix@gmail.com>`.
- **Non-root-relative links:** `tasks/ast-column-tracking.md:6`, `multi-file-load.md:39,62`,
  `mini-c-compiler.md:236`, `tasks/reference/mips-asm-teaching-style.md:27`,
  `tasks/reference/inherited-idiosyncrasies.md:44,59,64`.
- **History in reference docs:** the "History" sections of `tasks/reference/opcode-table-generator.md`
  and `tasks/reference/parser-ast-emit.md`. `tasks/reference/inherited-idiosyncrasies.md` is all FIXED items; it reads
  as a done-record, so move it to the archive, or trim it to what is still true. `CLAUDE.md:160` says
  "rollout is COMPLETE".
- **No Status line:** `ast-column-tracking`, `codebase-cleanup-plan`, `container-cross-env`,
  `examples-build-matrix`, `pgu-build-matrix`, `software-alu`. **No BLUF:** `mini-c-compiler`,
  `multi-file-load`, `string-equality-audit`, `timing-model`.
- Also: `tasks/reference/incremental-example-decomposition.md:115` cites gacalc's
  `algebra-teaching-order.md` with no URL (github.com/billsix/geometricalgebra).
- Conventions: the task-doc-conventions.md and reference-doc-conventions.md reference docs from
  github.com/billsix/runClaudeInContainer.

## Plan

1. Fix each item. Keep the "History" facts that still explain present design in a "Why" paragraph;
   drop the narrative.
2. No code changes.

## Open questions

1. `tasks/reference/inherited-idiosyncrasies.md`: (a) archive it as the record of the 2026-08 audit,
   or (b) trim it to the items still open? Recommend (b): the 2026-11-10 review's §3 table is the
   better record of what is open.
