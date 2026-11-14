# Spell out the abbreviated names in the Haskell port (`port/haskell/`)

**Status:** proposed — needs go-ahead
**Priority:** 6
**Difficulty:** 5
**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)
**Depends on:** `tasks/spelled-out-names-c-core.md` (its glossary is this task's input)

## BLUF

Apply the C core's spelled-out names to the Haskell engine port, so that a student reading the
port next to the C sees the same names in the same places. Done means every name the glossary
renames is renamed in the port, using Haskell's casing, plus any abbreviation that exists only
in the port. The port's parity and lint gates must pass.

## Request (William Emerison Six <billsix@gmail.com>, 2026-11-14)

"Once that's done for the C task, I want a follow up task for the python port, haskell port, and
rust port to do the same."

## Context

- Start only after `tasks/spelled-out-names-c-core.md` is archived. Its harvested glossary,
  `tasks/reference/spelled-out-names-glossary.md`, is the rename table: each abbreviated word, its
  spelling, and the exemptions with their reasons. Reuse it. Don't re-derive it.
- The ports mirror the C engine's names on purpose (`tasks/reference/engine-ports-design.md`), so a
  C rename maps to a port rename, with the casing changed to `camelCase` for values and functions
  and `CapWords` for types and constructors.
- Scope: `port/haskell/` (about 9,200 lines of Haskell). Coding standard: the shared
  `haskell-coding-standard.md` (github.com/billsix/runClaudeInContainer `tasks/reference/`).
- The same exemptions apply: externally defined names (`base` and library names, record fields a
  library dictates), text the program prints or reads (the goldens must stay byte-identical), and
  the tight-scope and familiar-acronym names the C task's review kept.

## Plan

1. **Discover the port's names mechanically:** GHC's `.hie` files (`-fwrite-ide-info`), the same
   source the `haskell-annotations` check reads, listing every binder. Save the command as
   `tasks/adhoc/spelled-out-names-haskell-port/discover.sh` and the result as a worklog under its
   `data/`.
2. **Map them through the glossary.** Each name is (a) a mirror of a renamed C name, renamed the
   same way, (b) a port-only abbreviation, added to the glossary with its meaning before renaming,
   or (c) exempt, with the reason recorded.
3. **Stop for the maintainer's review** of the port-only abbreviations and any new exemption.
4. **Rename in batches, one glossary word per commit:** the haskell-language-server rename over LSP,
   or a `.hie`-located rewrite; then `-Wall -Werror` confirms nothing dangles. Add each commit to
   `.git-blame-ignore-revs`.
5. **Gate every batch** with the port's suites from `port/README.md`: the regression and examples
   runs, `port/harness/compare_engines.py`, `compare_all_programs.py` and `compare_repl.py`, the
   float-format checks, and `port-haskell-hlint` (advisory) and `haskell-annotations`.
6. **Harvest:** add the port-only words to `tasks/reference/spelled-out-names-glossary.md`.

## Open questions

None yet. Step 3 settles the port-only cases.
