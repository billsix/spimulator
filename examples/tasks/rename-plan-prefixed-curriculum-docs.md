# Drop the `PLAN-` prefix from active curriculum task docs

**Status:** proposed — not started (2026-06-13)

## Why

Bill is retiring the old "plan files / handoffs" pattern in favour of plain
`tasks/` + `tasks/archive/<YYYY>/<MM>/<DD>/`. The completed curriculum plan docs
were archived, but the **active** ones in `examples/tasks/` were left with their
`PLAN-` prefix to avoid breaking the curriculum's internal cross-references in
one pass. They should be renamed to drop the prefix so they read as ordinary
task docs.

## Files to rename (examples/tasks/)

- `PLAN-build-matrix.md` → `build-matrix.md`
- `PLAN-container-cross-env.md` → `container-cross-env.md`
- `PLAN-libstr.md` → `libstr.md`
- `PLAN-multiarch-shim.md` → `multiarch-shim.md`
- `PLAN-symbol-tables.md` → `symbol-tables.md`
- `PLAN-unix-tools.md` → `unix-tools.md`

## Approach

`git mv` each, then grep the repo for references to the old `PLAN-<name>.md`
names and update them. Known referencers to check: `examples/READING-ORDER.md`,
`examples/tasks/SESSION_NOTES` (archived), other `examples/tasks/*.md`, the
`src/`/`Dockerfile`/`ChangeLog` provenance mentions (source-comment provenance can
stay as historical, but doc links should be fixed). Re-run a broken-link sweep
after. Coordinate with `../../tasks/fix-stale-doc-links.md` (overlapping link
cleanup).

Note: `build-matrix.md` already exists at the repo-root `tasks/` (the pgu copy);
this is the separate `examples/` curriculum build-matrix — keep them distinct
(they live in different task dirs).
