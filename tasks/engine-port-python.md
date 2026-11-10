# Port the simulator engine to Python, proven against the C engine

**Status:** ready — decisions made (Decisions); not started
**Priority:** 6
**Difficulty:** 8
**Created:** 2026-11-10 (from `tasks/archive/2026/11/10/engine-port-language-investigation.md`)
**Part of:** `tasks/engine-ports.md` (the C engine is the standard; `tasks/engine-port-haskell.md` is the sibling port)
**Source:** tasks/review/2026/11/10/repo-review.md §5, §6, §7 F2

## BLUF

Write a Python implementation of the spimulator engine (assembler, executor, syscalls, REPL, and
last the teaching mode) that behaves equivalently to the C engine, the standard: the same stdout, stderr and exit status on
the shared corpus, float-to-text differences being warnings only (`tasks/engine-ports.md`), checked by a differential harness in the
meson gate. This port also builds that harness, for every engine. Done means: the harness reports
zero functional differences on the whole corpus, and every example's
header shows how to run its `.asm` through `spimulator-python`.

## Context

- Language choice, the fixed-width-integer answer, subsystem map and the test plan:
  `tasks/reference/engine-port-language-choice.md` (read first). The language is Python (Decisions).
- What to match: audit record `tasks/review/2026/11/10/repo-review.md` §5 (behavior inventory) and §7
  (which oracle to trust).
- Old grammar, as a second syntax spec: `git show 6cb6a08:CPU/parser.y`, `git show 6bafa31^:src/parser.y`.

## Plan

1. **Prerequisites in C** (`tasks/engine-port-prereq-c-fixes.md`, before porting): fix the wrong-value bugs the record lists
   in §3 (F5) and pin the self-checking tests' full output as goldens (§6.4, F7), so the oracle is
   right and complete.
2. Harness first, shared by every port (`port/harness/`): a driver that runs each case through the
   C engine and each registered port (one command line per engine) and diffs stdout, stderr and exit
   status, wired as a meson suite. Corpus and rules: `tasks/engine-ports.md`.
3. Port in this order, each milestone gated by the harness: machine-arithmetic module → assembler
   (gate: AST-parity bytes) → executor + syscalls (gate: regression + examples) → CLI/REPL → explain.
4. Package layout, typing and test gate per the python-coding-standard.md reference doc from
   github.com/billsix/runClaudeInContainer (`ty`, ruff, doctests). The command is `spimulator-python`.
   Its interpreter comes from the unconditional `entrypoint/0N-install-ports.sh`
   (`tasks/engine-ports.md`); this port starts first, so it adds that script, listing every language.
5. When the harness is clean: the examples step from `tasks/engine-ports.md`, a `spimulator-python`
   line in every example header, checked by `tools/check_example_commands.py`.

## Findings

- **Python prints a negative NaN as `nan`; glibc prints `-nan`** (measured 2026-11-10 for
  `0xfff8000000000000` in `%.18g`, `%.8f` and `%#-13.6g`). On x86, `0.0 / 0.0` produces that NaN, so a
  MIPS program that divides zero by zero prints `-nan` in C. This difference would only be a
  warning (float-to-text does not gate a port; `tasks/engine-ports.md`). The fix is cheap and done:
  `tasks/adhoc/engine-ports-float-format/cformat.py`, the `%` operator plus a `-nan` sign fix, matches
  glibc exactly in all five of the C engine's float formats on 158,029 values. Promote it into the port.

## Decisions

- The port is in **Python** (William Emerison Six <billsix@gmail.com>, 2026-11-10).
- The C engine's wrong-value bugs (`tasks/review/2026/11/10/repo-review.md` §3, F5) are **fixed before
  the port**, so the port is compared against correct output (William Emerison Six <billsix@gmail.com>, 2026-11-10). Plan step 1.
- `,` as a floating-point decimal separator is **dropped** from the C scanner before the port (it
  makes `.word 1,2` a syntax error); the maintainer left the call to the agent, who recommended
  dropping it (William Emerison Six <billsix@gmail.com>, 2026-11-10). Tracked with the other
  step 1 prerequisites in `tasks/engine-port-prereq-c-fixes.md`.
- The port lives **in this repo**, under `port/`, so the harness builds the C oracle in the same
  image ("for now"; William Emerison Six <billsix@gmail.com>, 2026-11-10).
