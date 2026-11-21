# A Forth for spim, modelled on jonesforth, built as an incremental ladder like the calculator

**Status:** done 2026-11-21
**Created:** 2026-11-21 (William Emerison Six <billsix@gmail.com>)
**Done by:** Claude, on branch `forth-incremental-versions`, while the maintainer (William Emerison
Six <billsix@gmail.com>) was away and had authorized the whole task.

## What was done

A Forth interpreter in spim assembly, porting the design of Richard W.M. Jones's public-domain
jonesforth (version 47) to MIPS, was built as a nine-rung ladder in `examples/src/lang/forth/`:
`forth-1-next`, `-2-docol`, `-3-primitives`, `-4-dictionary`, `-5-numbers`, `-6-compiling`,
`-7-quit`, `-8-control`, `-9-prelude`. Each rung is a `.asm` + `.expected` (+ `.input` from rung 4)
and adds one idea, from a hand-written thread run by NEXT to a Forth that compiles jonesforth.f's
control structures, strings, CASE and the SEE decompiler from stdin. The per-rung spec, the register
map, the deviations from jonesforth and the wiring are in `tasks/reference/forth-incremental-versions.md`.

Also changed:

- `examples/tests/run-demo.sh` gained an asm-only path: a `forth-*` case arm sets `asm_only`, which
  skips the C run and requires the spim run to exit 0. The ladder is listed in
  `examples/src/meson.build`'s `lib_demo_tests`, so the examples suite and every port suite run it.
  `examples/src/Makefile` needed no change (it builds C only).
- `examples/READING-ORDER.md`: sub-entries 43a–43i after `calc-tree`, and a `lang/` row in the
  folder table.
- `tasks/reference/mips-asm-teaching-style.md`: the ladder added to the exempt list.
- `tasks/reference/incremental-example-decomposition.md`: the ladder noted as the one asm-only
  ladder.
- `CLAUDE.md` (examples layout) and `tasks/README.md` (dropped from the ordering list).

## Decisions

Recorded before the work (William Emerison Six <billsix@gmail.com>, 2026-11-21: "go with your
recommendations"): asm only, no C twins; jonesforth's 1/0 truth values; stop at rung 9 (`CATCH`/
`THROW`, `FORGET`, `DUMP` out of scope); exempt from the stack-visible asm style, with a register map
in each header; reading order right after `calc-tree`.

Made during the work (Claude, no questions asked, per the maintainer's instruction):

- **Registers:** IP `$s7`, return stack `$s6` over a 1 KiB `.data` buffer, data stack `$sp`, W `$t0`,
  as the task proposed. NEXT is written once at `next:` (spim has no `.macro`); primitives end
  `j next`.
- **User memory** is 64 KiB from `sbrk` at start-up rather than a `.space` buffer, so the `-dump`
  modes of `port/harness/compare_all_programs.py` do not dump a large data segment, and user words
  lie above the built-ins (jonesforth.f's `CFA>` needs that order).
- **Rungs 4–6 write INTERPRET as hand-threaded Forth** (it grows by one rule per rung) and rung 7
  replaces it with jonesforth's asm primitive. This keeps each of rungs 4–6 to one idea and makes
  rung 7's idea "the outer interpreter as jonesforth writes it, with errors".
- **Moved words between rungs** from the task's proposal: EXECUTE and EMIT to rung 4 (the rung-4
  outer loop and demo words need them); LITSTRING to rung 9 (only S" uses it, and rung 7 had no way
  to show it); `>DFA` and the `DOCOL`/`F_*` constants to rung 9 (TO, CONSTANT, WORDS and SEE need
  them).
- **Rung 8's asm is rung 7 minus the `.` and `CR` primitives;** the new code is jonesforth.f lines
  1–402, verbatim, at the top of `forth-8-control.input`. Rung 9 feeds lines 1–792 and 881–1107
  (FORGET and DUMP left out) and adds four asm words.
- **`PARSE ERROR: <word>` goes to stdout** (spim's print syscalls) and names the word rather than 40
  bytes of input context: KEY reads byte by byte, so there is no input buffer to show.
- **End of input exits 0** from inside `_KEY`, as jonesforth.

## Verification

- Every rung passed `examples/tests/run-demo.sh` under a scratch debug build of the C engine and
  under `port/python/spimulator-python`. Each golden was checked by hand against what the input
  should print before being pinned (rung 9's output was reviewed line by line).
- `make image` (the full gate) ran the whole meson test step green, 385 of 385 (the examples suite
  and the Python, Haskell and Rust port suites each ran all nine rungs, plus `compare_all_programs`
  over the new files), and the UBSan and ASan regression runs, 54 of 54 each.
- `make check-format` was not needed: no C, Rust, Haskell or port Python was touched.

## Files left for the archive commit

`tasks/adhoc/forth-incremental-versions/dictionary.py` printed the built-in dictionary block for
each rung while the rungs were written; it is one-shot and is removed with `git rm` in the archive
commit. The blocks it wrote are ordinary source now.
