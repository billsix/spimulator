# Examples: build with clang, and compile the assembly listings in debug mode

**Status:** proposed — needs go-ahead (open questions below)
**Priority:** 5
**Difficulty:** 3
**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)

## BLUF

The paired C demos under `examples/` are built and documented with gcc; switch them to clang, and
build the assembly they are compared with in a debug mode (`-g -O0`, frame pointer kept). That
includes the "BUILD AND RUN" header comment in each example `.c`, which today shows a one-line `gcc`
command (e.g. `examples/src/intro/helloworld/helloworld.c`). Done means every documented command
uses clang, the debug flags are in the meson build and in the comments, and the `examples` and
`documented-commands` meson suites pass.

## Context

- `examples/src/meson.build`: "Every teaching .c documents a one-line native gcc build" (the
  comment above the `documented-commands` wiring), `-O0` already set, and a note that the `.s`
  listings are built with no `-g`.
- `tools/check_example_commands.py` runs every header command verbatim (meson
  `documented-commands`), so the comments are tested.
- `examples/src/Makefile` is the second, independent build of the same demos (`CLAUDE.md` ›
  Layout); it has to change in step.
- Overlaps: `tasks/examples-c-debug-build-via-meson.md` (meson as the only build, debug builds for
  gdb/lldb; it would delete the Makefile) and `tasks/examples-build-matrix.md` (listings for five
  architectures). Settle the order with them.
- Teaching style: `tasks/reference/example-c-teaching-style.md`.
- The example C is also the input of `tasks/mini-c-compiler.md` (a C compiler that targets spim),
  so this task changes compilers and flags only, never the C itself: the demos stay inside that
  compiler's simple subset.

## Open questions

1. "Compile assembly in debug mode": the native `.s` listings generated from the C (add `-g` to
   their build, so the listings carry `.loc`/`.cfi` lines), or something about the hand-written
   MIPS `.asm` demos under spimulator? Recommendation: the generated listings.
2. Do this before, after, or as part of `tasks/examples-c-debug-build-via-meson.md`?
   Recommendation: as part of it, since both change the same build lines.
