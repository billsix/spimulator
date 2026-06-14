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

Each surface holds its **active** work at the top level and its
**completed** work under a date-bucketed
`archive/<YYYY>/<MM>/<DD>/` tree.  There is no separate handoff,
session-notes, or next-session journal: the files sitting at the
top of each surface *are* the live picture of what's open, and
git history plus the dated archive are the record of what's done.

Cross-references between the two surfaces use normal relative
paths (`../tasks/foo.md` from inside `examples/tasks/`, or
`../examples/tasks/PLAN-foo.md` from inside `tasks/`).

---

## Finding what to work on next

Everything at the top level of `tasks/` and `examples/tasks/`
(i.e. not under `archive/`) is open work.  Read the file's intro
and any `Status` line near the top or bottom; common markers:

| Marker | Meaning |
|---|---|
| `Status: Not started` | Not started; pick from here. |
| `Status: STAGED YYYY-MM-DD` | Changes staged but not committed (intermediate). |
| `Status: Partial` | Started; some sub-items still open. |

Quick survey of what's open on either surface:

```sh
# Open spim-side tasks
ls tasks/*.md

# Open curriculum tasks
ls examples/tasks/PLAN-*.md
```

Some of those may be reference docs rather than open work; read
the file's intro to tell.

Anything under `tasks/archive/` or `examples/tasks/archive/` is
done (or explicitly superseded).  Never pick from there; only
read for historical context.

---

## Cross-surface dependencies

Some tasks span both surfaces.  Examples seen so far:

- A curriculum demo (`examples/tasks/PLAN-*`) needing a spim
  feature (`tasks/foo.md`) as a prerequisite.  The curriculum
  plan typically links the spim task with a normal relative
  path.
- A spim CLI/parser fix driven by a curriculum-side use-case.
  The spim task references the example file via the relative
  `examples/src/...` path.

If a task references the other surface, follow the link to
understand the full picture before picking up.

---

## Logging completed work

When a task lands:

1. Update the plan doc's `Status` line to note it's done.
2. Move the file into that surface's
   `archive/<YYYY>/<MM>/<DD>/` directory (date = the day it
   landed).
3. Add a `ChangeLog` entry for any user-visible behavior
   change (the spim/examples convention is GNU-style:
   date+author header, then tab-indented topic blocks).
