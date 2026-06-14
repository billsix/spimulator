# Tasks

All task docs for this repo live here in `tasks/` — both **spim-internal** work
(simulator, parser, explain mode, REPL, command-line, exception handler, build
system) and **curriculum** work (example demos, library ports, READING-ORDER
updates, pedagogy decisions). They used to be split across `tasks/` and
`examples/tasks/`; that split has been folded into this one directory.

Convention: lowercase-hyphenated filenames (`cli-multi-file-load.md`,
`explain-stack-frame-offsets.md`, `multiarch-shim.md`). Active work sits at the
top level; completed work moves to `tasks/archive/<YYYY>/<MM>/<DD>/`. There is no
separate handoff / session-notes / next-session journal — the files at the top of
`tasks/` *are* the live picture, and git history plus the dated archive are the
record of what's done.

## Finding what to work on next

Everything at the top level of `tasks/` (not under `archive/`) is open work. Read
the file's intro and its `Status` line; common markers:

| Marker | Meaning |
|---|---|
| `Status: Not started` | Not started; pick from here. |
| `Status: STAGED YYYY-MM-DD` | Changes staged but not committed (intermediate). |
| `Status: Partial` | Started; some sub-items still open. |

Quick survey: `ls tasks/*.md`. Some may be reference docs rather than open work;
read the intro to tell. Anything under `tasks/archive/` is done (or superseded) —
read only for historical context.

## Logging completed work

When a task lands:

1. Update the doc's `Status` line to note it's done.
2. Move the file into `tasks/archive/<YYYY>/<MM>/<DD>/` (date = the day it landed).
3. Add a `ChangeLog` entry for any user-visible behavior change (GNU-style:
   date+author header, then tab-indented topic blocks).
