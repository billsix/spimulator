# Fix the doc claims the 2026-11-11 review found false

**Status:** proposed — needs go-ahead
**Priority:** 6
**Difficulty:** 2
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §1.2, §1.3, §1.4, §2.3, §2.4, §2.5, §2.6 F18

## BLUF

A batch of doc claims that don't match the tree. Done means each one below says what is true.

## Context

- **Gate wording** (§2.3): `CLAUDE.md` (the `-Werror` and conversion bullets),
  `tasks/reference/simulator-integer-types.md:225` and `meson.build:102` say the core is "gated" under
  gcc and clang. Every image build is `CC=clang`, so gcc is manual. `CLAUDE.md` says new C "is checked
  with" `tools/c_dispatch_check.py` and `tools/const_candidates.py`. No gate runs them, until
  `tasks/dispatch-check-gate.md` wires the first.
- **`tasks/reference/engine-port-language-choice.md`** (§1.2), still in the present tense:
  - :136 plans `tools/diff_engines.py`, but `port/harness/compare_engines.py` exists;
  - :218-221 and :256-257 require the glibc `qsort`/`bsearch` order, which the decode column now
    settles;
  - :96 lists `,` as an FP separator;
  - :99 cites `opcodes.h`;
  - :120 says "25 tests";
  - its double counts disagree (158,029 vs 58,028).

  The float-gate claims are fixed by `tasks/port-float-comparison-and-formatter-gates.md`.
- **`tasks/reference/engine-ports-design.md`** (§1.3): :51 should say `entrypoint/05-install-ports.sh`;
  :82 says 23 harness cases, and `port/harness/corpus.txt` has 24.
- **`tasks/reference/opcode-table-generator.md`** (§1.3): :53 and :145 cite the deleted
  `tasks/adhoc/opcodes-x-macro-redesign/check_decode.c`; point at the archived task's history instead.
- **Stale reference pointers** (§1.4): `calc-incremental-versions.md:4`,
  `incremental-example-decomposition.md:120`, `inherited-idiosyncrasies.md:7`,
  `mips-asm-teaching-style.md:27`, `port/harness/corpus.txt:14`.
- **`README.md`** (§2.4): :4 claims Windows support; :80 dates the copyright from commit
  `e10b97408f6d…`, which doesn't resolve here (see question 1); :37 uses a placeholder path where
  `helloworld.s` would work.
- **`port/README.md:29-31`** (§2.5) leaves out the `<port>-all-programs` comparison and
  `haskell-annotations`.
- **`tasks/reference/mips-fpu-and-float-demos.md:112-121`** (§2.6) says exit2's status isn't
  propagated; `src/syscall.c` now sets it. Verify, then fix.

## Plan

1. Fix each item; re-check every changed path with `ls`.
2. No code changes.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. `README.md:80` cites the maintainer's first commit `e2d4f30` (option (b)).
