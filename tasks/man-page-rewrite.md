# Rewrite Documentation/spim.1 from the flag parser, and install it as spimulator.1

**Status:** proposed — needs go-ahead
**Priority:** 7
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §2.2 F17

## BLUF

The man page documents a `-execute` flag that doesn't exist, omits six real flags, and installs as
`Documentation/spim.1` for a binary called `spimulator`, so `man spimulator` finds nothing. It is unchanged since
before the 2026-11-10 review (its F9). Done means `Documentation/spimulator.1` (renamed) lists exactly
the flags `src/spim.c` accepts, with the right program name, installed by meson.

## Context

- Flags in `src/spim.c` but not the man page: `-exception_file`, `-trap_file`, `-pseudo`, `-dump`,
  `-full_dump`, `-assemble`.
- In the man page but not `src/spim.c`: `-execute`.
- Its SYNTAX names `spim` (`Documentation/spim.1:5-8`); `meson.build:128` installs it.
- `tasks/explain-mode-correctness-bugs.md` (side findings) also wants the man page's explain-level text
  to match the code. Do both in one pass, or after that task.
- `src/README` and the usage text: the 2026-11-10 review's F9 also listed them.

## Plan

1. Generate the flag list from `src/spim.c`'s argument parser (a grep of `streq(argv[i], …)`), and
   write each entry from the code.
2. Rename the file to `spimulator.1`, update `meson.build`'s `install_man`, and add the ports' commands
   to SEE ALSO.
3. Check `man -l Documentation/spimulator.1` renders, and that `meson install` places it.

## Open questions

None.
