# Fix stale links in the archive README manifests

**Status:** proposed — not started (2026-06-13)

## Why

The task-doc reorganizations (flat `tasks/archive/*.md` → dated
`tasks/archive/<YYYY>/<MM>/<DD>/`; `plans`/handoff → `tasks/`; and folding
`examples/tasks/` into `tasks/`) moved many files. The in-doc links in the
**active** task docs, `tree-sitter/README.md`, and `examples/READING-ORDER.md`
have been repointed (a broken-link sweep over those is clean). What remains is
the archive index manifests, which still list **bare filenames** that now live in
dated subdirs.

## Remaining work

- `tasks/archive/README.md` — the manifest lists ~24 docs by bare filename
  (`merge-examples-into-spimulator.md`, `investigate-tree-sitter.md`,
  `argv-command-line-handling.md`, `teaching-mode*.md`, `HANDOFF-2026-05-1{7,22}.md`,
  …), all now under `tasks/archive/<YYYY>/<MM>/<DD>/`.
- `tasks/archive/README-examples-curriculum.md` — same problem for the curriculum
  archive (`PLAN-libctype.md`, `PLAN-tier1-tier2-tools.md`, …).

Decide a policy and apply it to both manifests: either (a) repoint each entry to
its dated path, or (b) replace the per-file lists with a note that the dated
`<YYYY>/<MM>/<DD>/` subdirs are authoritative and let `find`/git browse them.
Option (b) is lower-maintenance (the lists drift every time something archives).

Lower priority — links *inside* already-archived snapshots (frozen history, fix
only if convenient): e.g. `tasks/archive/2026/05/24/SESSION_NOTES.md`,
`tasks/archive/2026/05/22/c23-modernization.md`.

## Done check

Re-run a broken-link sweep over non-archived docs (should already be clean) plus
the two manifests after editing.
