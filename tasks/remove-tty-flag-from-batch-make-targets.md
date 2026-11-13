# Remove `-it` from the batch `make` targets that run a container

**Status:** proposed — needs go-ahead
**Priority:** 5
**Difficulty:** 1
**Created:** 2026-11-13 (William Emerison Six <billsix@gmail.com>)

## BLUF

Some Makefile targets pass `-it` (interactive + allocate a TTY) to `podman run` / `docker run` even
though they run a batch job that needs no terminal. Such a target fails where there is no terminal
(a CI runner, a cron job, `make format < /dev/null`): Docker refuses with "the input device is not a
TTY". Remove `-it` from those targets; keep it on `make shell` and the other targets listed below.
Done means the targets under "Remove `-it` from" have no `-it` / `-ti`, still work, and run with no
terminal attached.

## Context

The maintainer asked for this across all their projects (William Emerison Six
<billsix@gmail.com>, 2026-11-13): "make shell" targets should keep `-it`, and batch targets such as
`format` should not. The agent found these lines by scanning every tracked Makefile for a TTY flag on
a container `run` line (2026-11-13). Line numbers are as found then; match on the content, since the
lines may have moved.

### Remove `-it` from

- `Makefile:128` `format`
- `Makefile:151` the `run_in_container` macro, called by `html`, `pdf` and `epub` (`$(call run_in_container,...)`)
- `pgu/Makefile:71` `format`
- `pgu/Makefile:84` `html`
- `pgu/Makefile:93` `pdf`
- `pgu/Makefile:102` `epub`

### Keep `-it` on

- `shell` in `Makefile` and `pgu/Makefile` (interactive)

### Why some targets keep it

- **Anything that reads the keyboard needs a TTY:** an interactive shell, a REPL or TUI, a debugger,
  or a password prompt (for example `twine upload`).
- **Long-running GUI and server targets keep it too.** Without `-t`, Ctrl-C reaches only the
  container's PID 1, which ignores SIGINT unless it installs a handler, so the window or server does
  not stop. With `-t`, Ctrl-C goes to the app through the pty.
- **A batch target that finishes on its own needs neither**, so `-it` only makes it fail without a
  terminal.

### Repo-specific notes

- `tasks/github-actions-format-and-test-ci.md` needs `make format` to run on a GitHub runner, which has no TTY (its Context item 1). Doing this task first settles that item; that task then only adds `check-format`.
- `pgu/Makefile` is the book's own build; check whether it still routes through the interactive `aspell check` the top-level Makefile's comment describes, which would need a terminal regardless.

## Plan

1. Delete the `-it` (or `-ti`) from each line under "Remove `-it` from". Leave every other flag as is.
2. Re-grep the Makefile(s) for `-it` / `-ti` and confirm only the "Keep" targets still carry it.
3. Verify with no terminal attached: `make format < /dev/null | cat`, `make html < /dev/null | cat`. Each must exit as before; `< /dev/null | cat` gives the
   container no TTY on either side, so a leftover `-it` fails loudly.
4. Stage the Makefile(s). No doc change is expected unless a README or `CLAUDE.md` quotes the
   recipe.
