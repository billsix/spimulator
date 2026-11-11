# Rewrite 13 archived tasks into done-records; repoint dangling paths; remove one-shot ad-hoc scripts

**Status:** proposed — needs go-ahead
**Priority:** 6
**Difficulty:** 4
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §10.1, §10.2, §10.3, §10.4, §10.6 F13

## BLUF

Thirteen archived tasks still read as plans, several archived docs point at paths that moved, and the
port tasks' one-shot scripts are still tracked. Done means each listed doc is a past-tense done-record,
every path resolves, the superseded and "since done" notes are in place, and the one-shot
`tasks/adhoc/engine-port-*` scripts are `git rm`'d.

## Context

- **Never rewritten** (each keeps a plan-shaped BLUF, a Plan, Open questions or future tense):
  - `tasks/archive/2026/10/16/fix-inherited-idiosyncrasies.md` (five unchecked plan items under a
    SUPERSEDED status) and `tasks/archive/2026/10/16/integer-conversion-hardening.md`;
  - `tasks/archive/2026/10/30/calc-incremental-versions.md`, `tasks/archive/2026/10/30/examples-separate-io-from-algorithm.md`,
    `tasks/archive/2026/10/30/separate-io-from-algorithm-remaining-demos.md` and `tasks/archive/2026/10/30/incremental-example-versions.md`;
  - `tasks/archive/2026/10/31/examples-makefile-debug-build.md`, whose "settled" `DEBUG_FLAGS` at
    :59-62 contradict `examples/src/Makefile:64-68`;
  - `tasks/archive/2026/10/31/asm-simplification-sweep.md` and
    `tasks/archive/2026/10/31/asm-simplification-sweep-step-1-pilot-and-style-doc.md` (which also names a
    `tools/check_asm_traced_values.*` that became `tools/check_asm_canonical.py`);
  - `tasks/archive/2026/09/14/trim-claude-md.md`, `tasks/archive/2026/09/02/libstr.md`,
    `tasks/archive/2026/09/02/calc-language.md`;
  - `tasks/archive/2026/10/28/fix-examples-listing-std-flag.md`.
- **Dangling paths**, each now archived: `2026/10/16/fix-inherited-idiosyncrasies.md:4`,
  `2026/10/30/incremental-example-versions.md:22,95`, `2026/10/30/calc-incremental-versions.md:93`,
  `2026/11/10/engine-port-language-investigation.md:20`, `2026/11/10/engine-port-prereq-c-fixes.md:85`,
  `2026/11/10/engine-ports.md:67`. Relative links: `2026/08/03/parser-leak-cleanup.md:8`,
  `tasks/archive/2026/09/02/calc-language.md`, `tasks/archive/2026/09/02/libstr.md`.
- **Stale claims needing a note:**
  - `2026/11/10/engine-ports.md:43` ("not registered yet");
  - `engine-port-python.md:165-171` (install script and corpus "missing");
  - `engine-port-haskell.md:54-75` (planned answers reversed by its Decisions);
  - `2026/11/11/paired-single-compare-encoding.md:42` (future-tense image gate; it passed, 293/293);
  - `2026/11/11/python-port-match-statements.md:85-87` (wrong adhoc slug);
  - `2026/11/08/add-dockerignore.md:14-15` (MELPA step, removed the same day);
  - `2026/11/10/c23-modernization-pass3.md:43,49-50` (deferrals without a carry-over);
  - `2026/10/30/wc-add-golden-test-coverage.md:6-7` (stale Status).
- **One-shot scripts:** `git ls-files tasks/adhoc` lists `engine-port-haskell/` (2),
  `engine-port-python/` (3; its `tasks/adhoc/engine-port-python/add_port_lines.py` is a stale copy of `tools/add_port_lines.py`)
  and `engine-port-rust/` (3). The REPL ones feed `tasks/repl-transcript-corpus.md`. Remove them
  after that task, or keep them until then. `engine-ports-float-format/` and
  `integer-conversion-hardening/` are handled by `tasks/port-float-comparison-and-formatter-gates.md`.
- Conventions: the task-doc-conventions.md reference doc ("done-record rewrite") from
  github.com/billsix/runClaudeInContainer.

## Plan

1. Rewrite each listed doc into one past-tense voice; keep decisions and results; drop Plan, Open
   questions and Next.
2. Repoint every dangling path to its `tasks/archive/...` location, and add the dated notes.
3. `git rm` the `engine-port-*` one-shot scripts (once the REPL corpus covers the REPL ones).
4. Check: every backticked path in `tasks/archive/2026/{08,09,10,11}` resolves (a script like the
   review's).

## Open questions

None.
