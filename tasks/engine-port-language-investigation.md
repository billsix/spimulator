# Investigate porting the simulator engine from C to another language

**Status:** in progress — after the C23 pass
**Priority:** 5
**Difficulty:** 7
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Branch:** `c23-pass3-and-port-study`
**Source:** tasks/review/2026/11/10/repo-review.md §1, §5, §6, §7 F2

## BLUF

An investigation, not a port: how hard would it be to rewrite the **engine** (`src/` + `include/`,
not the example programs) in Python, Rust, Java, C#, Go, or GHC Haskell? Goal 1 is **compatibility**
with what the C does today; goal 2 is **readability for an upper-level undergraduate**. The
maintainer leans Python unless there is a strong reason otherwise, and is open to Rust or Java.
Deliverable: a reference doc `tasks/reference/engine-port-language-choice.md` with a recommendation,
a per-language effort estimate, the compatibility test plan, and a scaffolded follow-on port task.

## Context (read first)

- Behavior inventory to match: the port section of the audit record (from
  `tasks/source-and-history-deep-audit.md`) and `tasks/reference/inherited-idiosyncrasies.md`.
- Parser: the old flex/bison grammars (recoverable from git, commit named in the audit record) and
  the hand-written replacement (`tasks/reference/parser-ast-emit.md`). A port may use a parser
  generator (ANTLR) or a parsing library; the maintainer is not opposed.
- Integer semantics: `tasks/reference/simulator-integer-types.md`. The maintainer asks whether a
  language without fixed-width integers (Python) hurts a processor simulator — the doc must answer
  concretely (32-bit wraparound, signed/unsigned reinterpretation, `mult`/`div` HI/LO, IEEE float
  bit casts for the FPU, cost of masking everywhere vs. `ctypes`/`numpy` scalars).
- Test oracle: the C spimulator itself plus (a) `tests/` regression suite, (b) every historical test
  the audit recovers from git, and (c) **the example programs** (`examples/`, `pgu/`) run through
  both engines with output diffed — the maintainer named the samples as a good acceptance test. Method:
  the `porting-with-an-output-oracle.md` reference doc from github.com/billsix/runClaudeInContainer.

## Plan

1. Map each engine subsystem to each language: integer/float semantics, memory model, the
   `setjmp`-style exception paths, syscall I/O, the REPL/libedit, explain-mode output.
2. Spike the hard parts in the top 2–3 candidates (decode + execute a handful of instructions,
   32-bit arithmetic edge cases) to ground the estimates — throwaway code under
   `tasks/adhoc/engine-port-language-investigation/`.
3. Design the differential harness (C engine vs port over regression + historical + example
   programs).
4. Write the reference doc; scaffold `tasks/engine-port-<language>.md` as `proposed — needs go-ahead`.

## Decisions

- Run it the same night, after the C23 pass passes its gate: investigation only (reference doc + throwaway spikes), no port code (William Emerison Six <billsix@gmail.com>, 2026-11-10).
