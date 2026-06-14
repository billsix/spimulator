# Fix stale intra-repo doc links (archive reorg fallout)

**Status:** proposed — not started (2026-06-13)

## Why

The task-doc reorganizations (flat `tasks/archive/*.md` → dated
`tasks/archive/<YYYY>/<MM>/<DD>/`, and the `plans`/handoff → `tasks/` migration)
moved many files, but a number of relative markdown links that point at the old
locations were not updated, so they now 404. Pure documentation hygiene; no code
impact.

## Known broken links (audit + fix; sweep for any others)

Archive index READMEs still list bare filenames that now live in dated subdirs:
- `tasks/archive/README.md` — lines ~117, 144, 167–178 (`merge-examples-into-spimulator.md`,
  `investigate-tree-sitter.md`, `argv-command-line-handling.md`, `cli-multi-file-load.md`,
  `eof-signaling.md`, `explain-stack-frame-offsets.md`, `explain-missing-load-store-families.md`,
  `octal-escape-fix.md`, `post-phase5-naming-cleanup.md`, `teaching-mode.md`,
  `teaching-mode-coverage.md`, `unix-process-conformance.md`, `HANDOFF-2026-05-17.md`,
  `HANDOFF-2026-05-22.md`).
- `examples/tasks/archive/README.md` — lines ~15, 33 (`PLAN-libctype.md`, `PLAN-tier1-tier2-tools.md`).

Active task docs pointing at `archive/<name>.md` (now `archive/<date>/<name>.md`):
- `tasks/codebase-cleanup-plan.md:208` → `archive/c23-modernization.md`
- `tasks/repl-args-command.md:31` → `archive/argv-command-line-handling.md`
- `tasks/parser-leak-cleanup.md:127,128` → `archive/c23-modernization.md`, `HANDOFF-2026-05-22.md`
- `tree-sitter/README.md:15,112` → `../tasks/investigate-tree-sitter.md` (now archived)
- `examples/tasks/PLAN-container-cross-env.md:281` → `PLAN-asm-listings.md` (now archived)

Lower priority (links *inside* already-archived snapshots — frozen history, fix
only if convenient): `tasks/archive/2026/05/24/SESSION_NOTES.md`,
`tasks/archive/2026/05/22/c23-modernization.md`.

## Approach

Rewrite each link to the file's current path (use `git log --follow` or
`find tasks -name <basename>` to locate). Decide policy for the archive `README.md`
manifests: either repoint each entry to its dated path, or convert them to a note
that the dated subdirs are authoritative. Re-run a broken-link sweep to confirm
zero remain in non-archived docs.
