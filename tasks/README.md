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

---

## Finding what to work on next

Three things to check, in order:

### 1. The two "next session" / journal docs

- **`tasks/NEXT-SESSION.md`** — spim-side re-entry notes plus
  an "Open / declared stretch" queue.  Read top-to-bottom for
  the most recent context.
- **`../examples/tasks/SESSION_NOTES.md`** — curriculum
  chronological journal with per-session "Open queue (priority
  order, surfaced for next pick)" sections.  Search for the
  most recent date heading and read the "Open queue" near it.

If you only have time to read one thing, read NEXT-SESSION.md
for spim work or SESSION_NOTES.md for curriculum work.

### 2. Active plan docs (status markers)

Plan docs use a status convention near the top or at the
bottom.  Common markers:

| Marker | Meaning |
|---|---|
| `Status: Not started` | Not started; pick from here. |
| `Status: Landed YYYY-MM-DD` | Done; writeup is historical. |
| `Status: STAGED YYYY-MM-DD` | Changes staged but not committed (intermediate). |
| `Status: Partial` | Started; some sub-items still open. |
| `## Status — landed YYYY-MM-DD` | Header-style done marker. |

Quick survey of what's open on either surface:

```sh
# Open spim-side tasks (anything not marked landed)
grep -L -i 'LANDED\|Landed' tasks/*.md

# Open curriculum tasks (anything not marked landed)
grep -L -i 'LANDED\|Landed' examples/tasks/PLAN-*.md

# Or both surfaces at once
grep -L -i 'LANDED\|Landed' tasks/*.md examples/tasks/PLAN-*.md
```

(grep `-L` lists files WITHOUT the pattern — i.e., files that
DON'T say "Landed".  Some of those may still be reference
docs rather than open work; read the file's intro to tell.)

### 3. Anything in `archive/` is done

`tasks/archive/` and `examples/tasks/archive/` hold writeups
of completed (or explicitly superseded) work.  Never pick from
there; only read for historical context.  Each has its own
`README.md` summarizing what's archived.

---

## Cross-surface dependencies

Some tasks span both surfaces.  Examples seen so far:

- A curriculum demo (`examples/tasks/PLAN-*`) needing a spim
  feature (`tasks/foo.md`) as a prerequisite.  The curriculum
  plan typically links the spim task with a normal relative
  path (`../../tasks/foo.md` from inside an archived plan).
- A spim CLI/parser fix driven by a curriculum-side use-case.
  The spim task references the example file via the relative
  `examples/src/...` path.

If a task references the other surface, follow the link to
understand the full picture before picking up.

---

## Logging completed work

When a task lands:

1. Update the plan doc's `Status` line to `Landed YYYY-MM-DD`.
2. If it's a substantial multi-phase task (like
   `merge-examples-into-spimulator.md`), consider moving it to
   `archive/` and adding an entry to that surface's
   `archive/README.md`.  Small landed tasks can stay at top
   level.
3. Add a `ChangeLog` entry for any user-visible behavior
   change (the spim/examples convention is GNU-style:
   date+author header, then tab-indented topic blocks).
