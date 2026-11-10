# Every example C program documents a one-line native compile and a spimulator run

**Status:** proposed — open questions below
**Priority:** 3
**Difficulty:** 3
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Branch:** `c23-pass3-and-port-study`

## BLUF

Each teaching `.c` file under `examples/src/` gets a header block showing (1) how to compile it
natively on Linux with **one self-contained command** (no Makefile, no environment variable: the
full `gcc`/`cc` line with `-std=gnu23`, the `-I` include dirs, `-nostdlib`, the lib sources it
links), and (2) how to run its paired `.asm` in spimulator (the `spimulator -f …` line, with any
argv/stdin the demo needs). Today only a handful show either (e.g. `bubble-sort-1-fixed.c` shows a
`spimulator -f` line; none of the ~81 non-lib `.c` files shows a native compile line). Done means:
every file has both lines, each line was **run from the documented directory and works**, and a
small checker (`tools/`) keeps it that way.

## Context (read first)

- Flags and include/link inventory: `examples/src/Makefile` (`EDU_FLAGS`, `INCLUDES`,
  `LINK_FLAGS = -nostdlib`, the per-demo helper/lib lists) and `examples/src/meson.build`.
- Inputs/argv per demo: `*.input` files and `examples/tests/run-demo.sh`.
- Teaching style: `tasks/reference/example-c-teaching-style.md` — the header is student-facing.
- This was raised by the maintainer as part of the deep-audit request; it touches `examples/`
  only, so it is a separate task from the core audit.

## Plan

1. Discover (`discover.sh` under `tasks/adhoc/examples-c-compile-and-run-commands/`): per `.c`, the
   helper sources, libs, include dirs, run args/stdin, and whether it already has either line.
2. Generate the two lines per file; insert into the existing header comment in a consistent block.
3. Verify every documented command by running it verbatim from its directory, in the image.
4. Add `tools/check_example_commands.py` (extract + run the documented lines) and wire it into the
   examples suite if cheap (pending question).
