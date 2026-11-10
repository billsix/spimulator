# Deep audit of the simulator core and its full git history (C23 plan + port-behavior inventory)

**Status:** proposed — open questions below
**Priority:** 3
**Difficulty:** 6
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Branch:** `c23-pass3-and-port-study`

## BLUF

Run a deep repo audit (`/audit-repo deep` method) scoped to the **simulator core** (`src/`,
`include/`, `tests/`, `meson.build`, `Documentation/spim.1`) and its **entire git history**, with
special weight on the ~760 commits before the maintainer's first commit (James Larus et al.,
1998–2010s). Not `examples/`, not `pgu/`. The audit has two lenses, and one frozen record under
`tasks/review/2026/11/<DD>/repo-review.md` feeds two follow-on tasks:

1. **C23 lens** — every place the core could be cleaner in C23 (struct initialization, see
   `tasks/c23-modernization-pass3.md`). Output: the worklist that task implements.
2. **Port lens** — every user-observable behavior a port to another language must match
   (syscalls, exception/trap behavior, assembler quirks, REPL commands, flags, output formats,
   bug-compatible oddities). Output: the inventory `tasks/engine-port-language-investigation.md`
   consumes.

Done means: the record exists, the project `CLAUDE.md` points at it, and both follow-on tasks cite
its sections.

## Context (read first)

- Method, dimensions, record layout: the `repo-review-conventions.md` reference doc from
  github.com/billsix/runClaudeInContainer.
- Prior modernization: `tasks/archive/*/c23-modernization.md`, `.../c23-modernization-pass2.md`,
  `.../integer-conversion-hardening.md`. The core is already C23 + `-Werror` +
  `-Wconversion -Wsign-conversion` clean, so the C23 lens looks for what those passes left.
- Existing overlap: `tasks/codebase-cleanup-plan.md` (priority 4, an older cleanup/modernization
  triage) and `tasks/reference/inherited-idiosyncrasies.md` (behavior inherited from upstream SPIM).
  The audit reconciles against both rather than re-deriving them.
- Parser history: the flex/bison grammar was replaced by the hand-written parser
  (`tasks/reference/parser-ast-emit.md`, `tasks/archive/*/handwritten-parser-migration.md`). The
  last `.y`/`.l` files are recoverable from git (`git log --all -- '*.y' '*.l'`); the port lens
  records the commit to read them from.
- History: 1159 commits; authors larus/jameslarus/Jim/mlarus (upstream) and the maintainer. Use git
  for *why/when*, current code for *what is true now*.

## Plan

1. Fan out fresh readers per subsystem (scanner/parser/AST/emit, run+memory+exceptions,
   syscall+REPL, instruction codec/explain, tests) plus one history reader per era (upstream SVN
   era; maintainer era). Each returns `file:line`-anchored findings, tagged C23 / port / both.
2. History reader also harvests **compatibility tests**: every test file or test-like `.s` ever in
   the tree (`git log --all --diff-filter=D --name-only`, deleted `Tests/`, `tt.*.s`), with the
   commit to restore it from.
3. Verify any claim the record will be trusted on (dead code, "behavior X is relied on").
4. Write the frozen record; add the `CLAUDE.md` pointer.

## Open questions

See the closing list in chat; decisions get written here.
