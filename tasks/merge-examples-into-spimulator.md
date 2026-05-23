# Merge /examples into /spimulator

## Goal

Absorb `/examples/` (the paired C + MIPS assembly teaching
curriculum) into `/spimulator/examples/` as a literal mirror,
then unify the build / test / container story so:

- One Dockerfile (in /spimulator) builds spim AND the example
  demos AND runs the full test suite — image build FAILS if
  anything drifts.
- **The `.expected` (and `.expected-status`) golden files are
  the per-demo contract.  For every demo, the Dockerfile
  validates that BOTH the C side AND the spim-asm side
  produce byte-identical output matching the same golden —
  and where pinned, both produce the same exit status.  If
  either side drifts from the golden, the Docker build
  fails.**  This is what makes the golden files load-bearing
  instead of aspirational documentation.
- `meson test -C builddir` from /spimulator root runs every
  test, including the C-vs-asm goldens for the library demos
  (libctype, libstdlib, future libstr).
- Example .asm files use relative paths within the merged
  tree; no more `/spimulator/...` absolute references in
  /examples docs.
- `git log examples/` from /spimulator shows the full
  original /examples commit history with original
  authors/dates/messages preserved.

## Why

- `/examples` already depends on `/spimulator` (every demo's
  asm side can only run under spim).
- `/spimulator` already depends on `/examples` (most of the
  Unix-process conformance fixes, the multi-`-f` CLI work,
  the octal-escape bug, etc. were driven by example demos
  surfacing real-world papercuts).
- Today the test wiring across the two is manual: spim has
  meson tests; examples has none.  The library port
  (libctype + libstdlib v1 complete; libstr planned) has
  accumulated 6 paired demos with pinned `.expected` files but
  no automated pipeline to enforce them.
- One Dockerfile + one `meson test` is much easier to keep
  honest than two coordinated builds.
- Standalone /examples gets deleted after the merge — only
  one source of truth going forward.

## Decisions (locked 2026-05-23)

| Open question | Decision |
|---|---|
| Standalone /examples persistence | Interim only.  Deleted after merge. |
| `/examples/musl/` vendoring | Already deleted from /examples; no port needed. |
| Sphinx book pipeline (`/examples/book/`) | Drop — book not needed in merged tree. |
| Target path within /spimulator | Literal mirror — `/spimulator/examples/`. |
| Multi-`-f` doc paths | Update in phase 3 cross-reference sweep. |
| Task/plan layout | Two surfaces by domain: `/spimulator/tasks/` for spim-internal, `/spimulator/examples/tasks/` for curriculum.  No naming churn (PLAN- prefix stays for curriculum tasks). |
| Git history approach | Manual commit-by-commit replay (NOT subtree).  Preserves original author/email/date/message on every replayed commit. |

## Pre-flight (already verified)

- /spimulator is on branch `inlineExamples`, clean working tree.
- /examples has 27 commits on `master`, only untracked state
  is the orphan `tests/` dir from an earlier abandoned attempt.
- Replay script ready at `/tmp/replay-examples.sh`.

---

## Phase 1 — Replay /examples commits into /spimulator/examples/

**Status: LANDED 2026-05-23.**

Walked /examples's 27 commits oldest-first.  For each commit:

1. Wipe `/spimulator/examples/` contents (keep the dir).
2. Extract the /examples tree at that commit via
   `git -C /examples archive --format=tar <sha> | tar -xC /spimulator/examples`.
3. `git add -A examples` in /spimulator.
4. Commit with the original author/email/date/message preserved
   via `GIT_AUTHOR_*` + `GIT_COMMITTER_*` env vars and
   `git commit -F` for the message.

After all 27: verified every file in /examples HEAD exists
byte-identically at the corresponding `/spimulator/examples/`
path ("All files match" sentinel).

Script lived at `/tmp/replay-examples.sh` (ephemeral worktree
artifact; not committed).  Did NOT touch /examples (used
`git archive`, not checkout).  The orphan
`/examples/tests/run-demo.sh` (uncommitted) was NOT carried
over by this phase — it gets folded in explicitly in phase 5.

Gotcha encountered: `commit.gpgsign = true` in the global
gitconfig blocked the replay (no signing key available in the
container).  Disabled temporarily; re-enabled after the phase
completed.

After this phase the tree at `/spimulator/examples/` looks
like:
```
Dockerfile           # book-builder; goes in phase 2
Makefile             # book-builder; goes in phase 2
READING-ORDER.md
TEACHING-ASSEMBLER-INTERNALS.md
book/                # goes in phase 2
entrypoint/          # goes in phase 2
output/              # build artifact; goes in phase 2 if present
src/
tasks/
```

The first /examples commit (`c6697c0 import examples from
https://github.com/billsix/spimulator`) and all 26 subsequent
commits are visible via `git log examples/` from
/spimulator, with their original author / email / date /
message preserved.

---

## Phase 2 — Prune what's not wanted

Delete from the new `/spimulator/examples/`:

- `book/` — Sphinx book pipeline (not needed)
- `entrypoint/` — book-build script (not needed)
- `Makefile` — book build orchestration (not needed)
- `Dockerfile` — book-only image (replaced by /spimulator/Dockerfile)
- `output/` — build artifact, if present
- `musl/` — should not be present (was deleted from /examples
  before merge), but verify and clean up any stragglers

Keep: `src/`, `tasks/`, `tests/` (the orphan run-demo.sh
arrives in phase 5), `READING-ORDER.md`,
`TEACHING-ASSEMBLER-INTERNALS.md`, `SESSION_NOTES.md` (if at
top level), `.gitignore`.

One commit titled something like "Drop book pipeline + other
unused trees post-merge."

---

## Phase 3 — Cross-reference sweep

Update relative paths now that the curriculum tree lives one
level deeper inside /spimulator:

- `../spimulator/tasks/...` → `../../tasks/...` (going up
  two: from `examples/tasks/foo.md` → `examples/` → `/spimulator/`
  → `tasks/`).  Or simpler: drop the `..` traversal entirely
  by using absolute-from-repo-root.
- `/spimulator/...` absolute paths in moved files → relative
  to the merged tree.  E.g., from inside
  `examples/tasks/PLAN-libstdlib-atexit.md`,
  `/spimulator/tasks/cli-multi-file-load.md` becomes
  `../../tasks/cli-multi-file-load.md`.
- Invocation examples throughout `READING-ORDER.md`,
  `SESSION_NOTES.md`, and `examples/tasks/PLAN-*.md`:
  `spimulator -f /spimulator/.../libctype.asm` →
  `spimulator -f examples/src/lib/libctype/libctype.asm`
  (or whatever's natural relative to expected cwd).
- Per-file `.asm` and `.c` header comments that show
  invocation patterns — verify each.

Tooling: `grep -rln '/spimulator/' examples/`,
`grep -rln '\.\./spimulator/' examples/`, manual fix-up pass.

One commit per file area (one commit each for READING-ORDER,
SESSION_NOTES, the tasks dir sweep, the demo source sweep).
Or one big commit if the changes are mechanical.

---

## Phase 4 — Task & plan merger

Layout after merge:
```
/spimulator/
    tasks/                  (spim-internal tasks)
        archive/
    examples/
        tasks/              (curriculum tasks: PLAN-*.md)
            archive/
```

Two surfaces by domain — different review burden, different
reader expectations.  No file moves, no renames.  Just:

- Update `/spimulator/tasks/merge-examples-into-spimulator.md`
  status → landed.
- Update `/spimulator/tasks/NEXT-SESSION.md` open queue to
  reference `examples/` paths (was `/examples/`).
- Update `/spimulator/tasks/HANDOFF-2026-05-22.md` only if
  worth keeping current; otherwise leave as historical.
- Optional: a one-line index in
  `/spimulator/tasks/README.md` (or similar) pointing at
  `examples/tasks/` for curriculum tasks.
- Cross-references between the two task surfaces become
  normal relative paths (`../tasks/...` from inside
  `examples/tasks/`).

---

## Phase 5 — Build integration

- `/spimulator/meson.build` adds `subdir('examples/src')` so
  `meson compile -C builddir` from /spimulator root builds
  the demos too.  Likely needs to convert
  `examples/src/meson.build`'s top-of-file `project(...)`
  declaration to a continuation (drop the project line; the
  parent project owns it).
- The orphan `tests/run-demo.sh` from /examples lands here.
  Goes into `/spimulator/examples/tests/run-demo.sh`,
  preserved as-is.  **For each demo, the script runs both
  the C executable AND `spimulator -f <libs...> -f <demo>.asm`,
  diffs both stdouts against the same `<demo>.expected` file,
  and (for exit-demo and atexit-demo) verifies both exit
  statuses match `<demo>.expected-status`.**  The script
  fails fast on any mismatch — that's what makes the goldens
  load-bearing.
- Demo `test()` entries: 6 tests today
  (ctype-demo, atoi-demo, abs-demo, exit-demo, bsearch-demo,
  atexit-demo).  Registered in the merged
  `examples/src/meson.build`.  Each `test()` invokes
  `run-demo.sh` with the demo's name; the script handles
  the both-sides-against-golden contract.
- `meson test -C builddir` from /spimulator root now runs:
  - spim's 23 existing regression tests
  - the 6 demo tests — each of which validates **both
    C and asm** against the pinned `.expected` golden
  - = 29 total

---

## Phase 6 — Dockerfile rewrite

`/spimulator/Dockerfile` becomes the canonical one:

- Existing spim deps: gcc, meson, ninja-build, flex, bison,
  libedit-devel (Fedora 44)
- Add: `diffutils` (run-demo.sh uses `diff -q`)
- Build spim first, `meson install` so the binary is on PATH
- Build the example demos (implicit via the unified meson
  setup, once subdir is wired)
- Run `meson test -C builddir` — image build FAILS if anything
  drifts.  **The 6 demo tests in that run each validate that
  the C-compiled binary AND the spim-asm version BOTH produce
  output matching the same `<demo>.expected` golden file (and
  matching `<demo>.expected-status` for the two
  exit-status-pinned demos).  Any drift on either side —
  C-side regression, asm-side regression, or the two diverging
  from each other — fails the Docker build at this step.**
  This is the load-bearing guarantee.

Drop `/spimulator/examples/Dockerfile` (was the book builder;
already removed in phase 2).

---

## Phase 7 — Final verification

- `meson test -C builddir` from /spimulator root: 29/29 pass.
- `podman build -t spimulator .` succeeds end-to-end.
- Spot-check a few demos by hand:
  - `spimulator -f examples/src/lib/libctype/libctype.asm
       -f examples/src/lib/libctype-demo/ctype-demo.asm`
  - Same for atoi/abs/bsearch/exit/atexit
- `git log examples/` shows the 27 historical /examples
  commits plus the post-merge cleanup commits.
- After all the above passes: tell Bill it's done so he can
  delete the standalone /examples repo.

---

## What could go wrong

- **Phase 1 replay**: a commit message containing `%H` or
  similar git format specifiers could break shell quoting.
  The script uses `-F file` (heredoc-style via file) to avoid
  this, but heads-up.
- **Phase 5 meson surgery**: `subdir(...)` requires
  `examples/src/meson.build` to NOT have its own `project()`
  call.  Easy fix — drop the line — but worth confirming.
- **Phase 6 Dockerfile**: the build assumes spim's `meson
  install` puts the binary at a path the example tests can
  find.  Currently the install target is `/usr/local/bin/`
  by default; verify before relying on it.
- **The standalone /examples user-side**: Bill needs to NOT
  delete /examples until phase 7 verification confirms
  everything works.  Keep both around until the merge is
  proven.

## Out of scope

- New library functions (libstr) — can land before OR after
  this merge.
- Any code refactor of spim itself or the library
  implementations.

## Status

Plan written 2026-05-23.  Awaiting Bill's confirmation to
proceed with phase 1.  Will execute phase-by-phase, with
verification between phases (so each phase can be reviewed
or backed out independently).
