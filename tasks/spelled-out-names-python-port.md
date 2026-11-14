# Spell out the abbreviated names in the Python port (`port/python/`)

**Status:** proposed — needs go-ahead
**Priority:** 6
**Difficulty:** 5
**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)
**Depends on:** `tasks/archive/2026/11/14/spelled-out-names-c-core.md` (done; its glossary is this task's input)

## BLUF

Apply the C core's spelled-out names to the Python engine port, so that a student reading the
port next to the C sees the same names in the same places. Done means every name the glossary
renames is renamed in the port, using Python's casing, plus any abbreviation that exists only
in the port. The port's parity and lint gates must pass.

## Request (William Emerison Six <billsix@gmail.com>, 2026-11-14)

"Once that's done for the C task, I want a follow up task for the python port, haskell port, and
rust port to do the same."

## Context

- The C task is done: `tasks/archive/2026/11/14/spelled-out-names-c-core.md`. Its glossary,
  `tasks/reference/spelled-out-names-glossary.md`, holds each abbreviated word, its spelling, the
  conventions, and the exemptions with their reasons. Reuse it. Don't re-derive it. The per-name
  table is `tasks/adhoc/spelled-out-names-c-core/data/renames.tsv` (old, new, kinds, source,
  location), which maps each C name to its new spelling.
- **Already done by the C task:** the names `tools/gen_opcodes.py` generates for the port (the
  token and operand-shape names of its opcode tables) were renamed with the C, and the port's
  references to them followed. This task renames the port's own names.
- The last of the three port tasks to be archived `git rm`s
  `tasks/adhoc/spelled-out-names-c-core/` in its archive commit, since the ports were the reason
  to keep it.
- The ports mirror the C engine's names on purpose (`tasks/reference/engine-ports-design.md`), so a
  C rename maps to a port rename, with the casing changed to `snake_case` (already C's casing) and
  `CapWords` for classes.
- Scope: `port/python/` (about 9,900 lines of Python). Coding standard: the shared
  `python-coding-standard.md` (github.com/billsix/runClaudeInContainer `tasks/reference/`).
- The same exemptions apply: externally defined names (the standard library, `argparse` options,
  dunder methods), text the program prints or reads (the goldens must stay byte-identical), and the
  tight-scope and familiar-acronym names the C task's review kept.

## Plan

1. **Discover the port's names mechanically:** Python's `ast` module over the port's modules (every
   `Name` store, `FunctionDef`, `ClassDef`, `arg`, `Attribute` store and module constant). Save the
   command as `tasks/adhoc/spelled-out-names-python-port/discover.sh` and the result as a worklog
   under its `data/`.
2. **Map them through the glossary.** Each name is (a) a mirror of a renamed C name, renamed the
   same way, (b) a port-only abbreviation, added to the glossary with its meaning before renaming,
   or (c) exempt, with the reason recorded.
3. **Stop for the maintainer's review** of the port-only abbreviations and any new exemption.
4. **Rename in batches, one glossary word per commit:** rope's rename refactoring, or an
   `ast`-located rewrite; then `ty` and ruff confirm nothing dangles. Add each commit to
   `.git-blame-ignore-revs`.
5. **Gate every batch** with the port's suites from `port/README.md`: the regression and examples
   runs, `port/harness/compare_engines.py`, `compare_all_programs.py` and `compare_repl.py`, the
   float-format checks, and `port-python-lint` (`port/python/lint.sh`).
6. **Harvest:** add the port-only words to `tasks/reference/spelled-out-names-glossary.md`.

## Open questions

None yet. Step 3 settles the port-only cases.
