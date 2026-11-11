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

Everything at the top level of `tasks/` (not under `archive/`) is open work. Each task's header has a
`**Status:**` line, plus `**Priority:**` (1 = do first) and `**Difficulty:**` (1 = easiest), both on
a rough geometric 1-10 scale. Pick by lowest Priority, then lowest Difficulty. The statuses in use:

| Status | Meaning |
|---|---|
| `proposed — needs go-ahead` | Written and ready; waits for the maintainer's go-ahead (and answers to any Open questions). |
| `proposed — investigate first …` | The first step is research; the plan comes after. |
| `blocked` | Waits on something outside this repo; `**Blocked on:**` and `**Recheck:**` say what and how to test it. |
| `audit COMPLETE …` | The work is done and waits on a decision from the maintainer. |

A task that waits on another task in this repo is not `blocked`: it carries `**Depends on:**` and a
higher Priority number.

Quick survey: `grep -m1 -H '^\*\*Status' tasks/*.md`. Anything under `tasks/archive/` is done (or
superseded); read it only for history.

## Ordering & dependencies (reviewed 2026-11-11)

Most open tasks are independent. The ones that aren't:

**Chains (do left before right):**

- `explain-levels-redesign` → `engine-ports-teaching-mode`. The redesign starts from correct output
  (the bugs were fixed in `tasks/archive/2026/11/11/explain-mode-correctness-bugs.md`; target design:
  `tasks/reference/teaching-mode-learner-model.md`); the ports get the teaching mode last.
- `examples-c-debug-build-via-meson` decides the fate of `examples-build-matrix`,
  `container-cross-env` and `pgu-build-matrix` (its open questions 1-3).
- `mini-c-compiler` (capstone) after the shared asm library work in `multi-file-load`; its
  acceptance harness is the demo goldens.

**Every C behavior change follows in every port** (`CLAUDE.md`): `ast-column-tracking`,
`explain-levels-redesign`, `timing-model` and `software-alu` each have a ports step. The port suites
fail until each port matches.

**Independent, any order:** `timing-model` and `software-alu` (share cycle vocabulary if both land;
`timing-model` is much cheaper), `ast-column-tracking`, `string-equality-audit` (waits on the
maintainer), `paired-single-instructions-unsupported` (on hold), `big-endian-conversion-hardening`
(blocked).

## Logging completed work

When a task lands:

1. Rewrite the doc into a past-tense done-record (drop the Plan, Open questions and Status line).
2. Move it with `git mv` into `tasks/archive/<YYYY>/<MM>/<DD>/` (date = the day it landed), in its
   own commit after the work, and repoint any links to its old path.
