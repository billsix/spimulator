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

## Ordering & dependencies (reviewed 2026-07-07)

Most open tasks are independent; these are the ones that aren't, plus a
sensible grouping.  (Statuses verified against the code on the date above —
several docs' internal claims were refreshed the same day.)

**Chains (do left before right):**

- `parser-leak-cleanup` → `ast-column-tracking` — the leak fix deletes the
  PARSE_DIRECT codepath; write the column plumbing once, after.
- `libstr` → `multi-file-load`'s de-dup sweep → new `unix-tools` demos draw
  from the shared library.  (The multi-file *loading mechanism* already
  works — `tests/tt.multifile.s` — so nothing blocks libstr.)
- Naming: `opcode-types-descriptive-names` + `codebase-cleanup-plan` Tier
  B2/B3 as **one** mechanical-rename sweep → then `c23-modernization-pass2`
  (don't modernize code about to be renamed).
- Example hygiene, suggested order: `string-equality-audit` (suspected real
  bug) → `code-idiosyncrasies-audit` (reads everything).  The embedded-C
  removal (`c-asm-comment-parity`) landed 2026-07-07 and is archived.
- `mini-c-compiler` (capstone) comes after the library chain — its output
  links against the shared asm library, and its acceptance harness is the
  demo goldens.

**Explicitly NOT dependencies (verified):**

- `examples-build-matrix` / `pgu-build-matrix` no longer wait on anything —
  the multiarch shim landed (`crt0.h`, archived 2026-07-07) and the listing
  matrices need only clang, which the image has.  `container-cross-env`
  (lld + qemu-user-static) is required only for the *runtime* verification
  track, not the matrices; coordinate the MIPS-endianness choice across all
  three whenever they land.
- `timing-model` and `software-alu` are independent of everything and each
  other; if both land they must share cycle vocabulary.  `timing-model` is
  much cheaper — do it first if the H&P performance lesson is the goal.

**Independent quick wins, any time:** all of the original list landed and
archived 2026-07-07 (container-build-cleanup, container-aslr-lldb,
fix-stale-doc-links, stdin-space-separated-ints, program-listing-at-start,
string-stream-to-memstream, examples-install-location, plus
opcode-types-descriptive-names and the stdlib-modernization pass).
Current smallest open items: `unix-tools`' remaining `strings` + hash
demos, and `rpn-calculator`.

## Logging completed work

When a task lands:

1. Update the doc's `Status` line to note it's done.
2. Move the file into `tasks/archive/<YYYY>/<MM>/<DD>/` (date = the day it landed).
3. Add a `ChangeLog` entry for any user-visible behavior change (GNU-style:
   date+author header, then tab-indented topic blocks).
