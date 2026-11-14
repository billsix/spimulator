# Make the man page and docs cover the full feature set

**Status:** proposed — needs go-ahead
**Priority:** 5
**Difficulty:** 3
**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)

## BLUF

Check `Documentation/spimulator.1` (and `README.md`) against every flag, REPL command, syscall and
mode the simulator actually has, and fill the gaps. Done means every option the argument parser
accepts and every REPL command is in the man page, nothing documented is missing from the code, and
a check keeps it that way if one is cheap.

## Context

- The man page's sections: NAME, SYNOPSIS, DESCRIPTION, OPTIONS, EXIT STATUS.
- The flags are parsed in `src/spim.c`; the REPL commands are there too. Teaching mode's `-explain=N`
  levels are described in `CLAUDE.md` › Status and `src/explain.c`.
- The engine ports take the same flags (`port/README.md`), so the man page is their reference too.
- A lead for a check: list the option strings in `src/spim.c` and fail if one is absent from the
  man page, as `tools/check_example_commands.py` does for the example headers.
