# Investigate porting the simulator engine from C to another language

**Priority:** 5
**Difficulty:** 7
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-10
**Branch:** `c23-pass3-and-port-study`
**Source:** tasks/review/2026/11/10/repo-review.md §1, §5, §6, §7 F2

## Outcome

An investigation, not a port. The findings are in `tasks/reference/engine-port-language-choice.md`:
the recommendation (Python, with Rust as the alternative; Java and Haskell not recommended because
their `%.18g` prints wrong digits), a per-language compatibility/readability/effort table, the
concrete answer on fixed-width integers (no harm in Python if one module normalizes every 32-bit and
single-precision result; the traps are `//`/`%` flooring and binary64 single-precision arithmetic),
a subsystem map, the differential-test plan (regression suite, AST goldens, the never-executed
upstream bare-machine tests, `allinsts.s` from `f570a87^`, the examples and PGU programs), and the
restore commands for the old flex/bison grammar. The follow-on port is scaffolded as
`tasks/engine-port-python.md` (proposed — needs go-ahead; the language is its first open question).

## How it was done

- The behavior inventory came from the deep audit (`tasks/review/2026/11/10/repo-review.md` §5–§7).
- Throwaway spikes (since deleted) under `tasks/adhoc/engine-port-language-investigation/`: a C
  reference (`ref.c`) printing 26 edge-case results (wraparound, overflow traps, shifts,
  `mult`/`multu`, `div` truncation and its zero/INT_MIN guards, `lb`/`lbu`, float bit casts,
  single-precision rounding, `cvt.w.s`, spim's `round.w.s`, `%.8f`/`%.18g`) plus a decode+execute loop
  over seven real MIPS encodings (~9M instructions); Python, Rust and Java versions; and `run.sh`,
  which diffed each against the C. All three matched byte-for-byte. Timings: C 0.008 s, Rust 0.011 s,
  Java 0.021 s, Python 1.73 s.
- Formatting probes for Go, C# and GHC: Go and C# format `%.18g` like glibc; GHC's `printf` does not.
- Measured the C engine on the suites: every test runs in ≤ 0.21 s, so interpreter speed is not a
  constraint.

## Decisions

- Run it the same night, after the C23 pass passes its gate: investigation only (reference doc + throwaway spikes), no port code (William Emerison Six <billsix@gmail.com>, 2026-11-10).
- The spikes covered the top three candidates (Python, Rust, Java); Go, C# and Haskell got formatting
  probes only, since their integer types settle the width question by construction.
- `tasks/codebase-cleanup-plan.md` Tier G2 mentions a "Go-port survey"; it is not in the tree or the
  history (`git log --all -i --grep=survey` finds nothing relevant), so nothing was reused from it.
