# Tasks

Two task surfaces live in this repo:

- **`tasks/`** (this directory) — work on **spim itself**:
  simulator changes, parser, explain mode, REPL, command-line,
  exception handler, build system.  Convention:
  lowercase-hyphenated filenames (`cli-multi-file-load.md`,
  `explain-stack-frame-offsets.md`).
- **`../examples/tasks/`** — work on the **curriculum**:
  example demos, library ports (libctype, libstdlib, libstr,
  …), READING-ORDER updates, pedagogy decisions.  Convention:
  `PLAN-`-prefixed filenames (`PLAN-libctype.md`,
  `PLAN-libstdlib.md`).

Each surface has an `archive/` subdirectory holding completed
or superseded plan docs.  Active items sit at the top level.

Cross-references between the two surfaces use normal relative
paths (`../tasks/foo.md` from inside `examples/tasks/`, or
`../examples/tasks/PLAN-foo.md` from inside `tasks/`).
