# Rewrite Documentation/spim.1 from the flag parser, and install it as spimulator.1

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`
**Source:** tasks/review/2026/11/11/repo-review.md §2.2 F17

## Outcome

`Documentation/spim.1` is now `Documentation/spimulator.1`, and `meson.build` installs it under that
name (`share/man/man1/spimulator.1`). It was installed as `spim.1` for a binary called `spimulator`,
so `man spimulator` found nothing.

It is rewritten from `src/spim.c`'s argument parser:
- every option, with its short form, grouped: the machine, the exception handler, messages, teaching
  mode, memory-mapped IO, memory sizes, loading and running, and what the assembler builds;
- new sections: the exit status (the program's, 128 plus the exception code, 2, 1, 255), the ports,
  and the default exception handler.

Fixed on the way:
- `-execute`, which doesn't exist, is gone;
- `-exception_file`/`-ef`, `-trap`/`-notrap`/`-trap_file`, `-pseudo`/`-nopseudo`, `-assemble`,
  `-dump` and `-full_dump` are documented;
- `-quiet` and `-noquiet` were described the wrong way round;
- the old `spim` name, xspim/qtspim, and the dead SourceForge contact are gone.

The teaching-mode level descriptions are kept, as they match the code.
`tasks/explain-mode-correctness-bugs.md` will update them with its fixes. `CLAUDE.md`, the
`meson.build` comment and two open tasks cite the new name.

## Not done (follow-up)

The engine's usage text (`src/spim.c`, printed for a bad argument) has the same gaps: it says
`Usage: spim`, and leaves out the size, trap and pseudo flags. The three ports print the same text,
and the harness compares it, so fixing it is a four-engine change. No task was filed.

## Verification

`man -l Documentation/spimulator.1` renders. `meson install --destdir` places
`share/man/man1/spimulator.1`. Each documented flag was checked against `src/spim.c`.
