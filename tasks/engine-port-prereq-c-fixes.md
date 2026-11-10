# Before the Python port: fix the C engine's wrong-value bugs, pin the test oracle, drop `,` as an FP decimal separator

**Status:** proposed — needs go-ahead
**Priority:** 3
**Difficulty:** 5
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/10/repo-review.md §3 (F5), §6 (F7), §3.20 (F12)
**Part of:** `tasks/engine-ports.md`
**Next:** `tasks/engine-port-python.md` (its Plan step 1 is this task), then `tasks/engine-port-haskell.md`

## BLUF

The maintainer decided the C engine's bugs get fixed before the Python port, so the port is diffed
against correct output, and that `,` stops being accepted as a floating-point decimal separator.
This task does three things in C: it fixes the F5 wrong-value bugs, each with a regression test; it
closes the F7 test-gate gaps, so the oracle is complete; and it drops the `,` separator. Done means:
every F5 item has a failing-then-passing test, the F7 tests run in the meson regression suite,
`.word 1,2` assembles two words, and `make image` (sanitizers on) is green.

## Context (read first)

- `tasks/review/2026/11/10/repo-review.md` — §3 rows 3.4, 3.5, 3.7, 3.8, 3.10, 3.13 (F5), §6.1, §6.2,
  §6.4–§6.6 (F7), §3.20 (the separator; upstream `9b0892d`, `8141259`). The record is frozen; re-check
  each row against the tree before acting.
- `tasks/explain-mode-correctness-bugs.md` owns the explain-mode bugs (F6); not this task.
- The memory-safety bugs (F3) and the setjmp locals (F4) do not change output on correct programs,
  so they do not gate the port; they are left for their own task.

## Plan

1. **Separator (F12, §3.20):** remove `,` from the FP-literal scan in `src/scanner.c`; decide `'`
   (§3.20 mentions both) in the same change. Test: `.word 1,2` assembles to two words; an FP literal
   with `.` still parses. Note the change in `Documentation/spim.1` if it documents the separator.
2. **F5, one commit per bug, test first:** `divu` overflow guard, forward `.word lab+N`, `c.cond`/`movf`
   decode, `movn.s`/`movz.s` encoding, `BRANCH_OFFSET` reach, signed-overflow UB in `signed_multiply`
   and in text-segment `sb`/`sh`. Any `opcodes.h` change is gated with `BUILD_TREE_SITTER=1` (`CLAUDE.md`).
3. **F7:** run `tt.alu.bare.s`/`tt.fpu.bare.s` under `-bare -noexception`; make the `bare` test pass
   `-bare`; make the AST-parity loop fail on a missing dump; pin `tt.core.s`'s full output as a golden;
   add one default-handler (no `-ef`) test; fix or delete `explain_parity`.
4. Gate: `make image` with the default flags (sanitizers on).

## Decisions

- Fix before porting, and drop `,` as a decimal separator (William Emerison Six <billsix@gmail.com>,
  2026-11-10; recorded in `tasks/engine-port-python.md`).
