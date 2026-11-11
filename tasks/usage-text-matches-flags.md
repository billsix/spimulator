# Make the usage text list every flag, in all four engines

**Status:** proposed — needs go-ahead
**Priority:** 7
**Difficulty:** 2
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Found by:** `tasks/archive/2026/11/11/man-page-rewrite.md`

## BLUF

The usage text the engine prints for a bad argument (`src/spim.c`, the usage string near `-assemble`)
says `Usage: spim` and leaves out `-stext`/`-sdata`/`-sstack`/`-sktext`/`-skdata`, `-ldata`/`-lstack`/
`-lkdata`, `-trap`/`-notrap`/`-trap_file`, `-pseudo`/`-nopseudo` and the short forms. Done means it
names the program `spimulator` and lists every option the parser accepts, as
`Documentation/spimulator.1` does, in C and in the three ports, with the port suites green.

## Context

- `Documentation/spimulator.1` is the complete list, written from the parser on 2026-11-11.
- The ports print the same usage text, and the harness compares stderr (the `missing_flag_value`
  corpus case), so the change goes into `port/python/`, `port/haskell/` and `port/rust/` with C.
- Reproduce: `spimulator -h` prints "Unknown argument: -h (ignored)" and then the usage.

## Plan

1. Rewrite the usage string from the man page's option list, keeping one line per option.
2. Make the same change in each port; run `meson test` (regression and the port suites).

## Open questions

None.
