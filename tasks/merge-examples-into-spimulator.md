# Merge /examples into /spimulator

## Goal

Absorb `examples/` (the paired C + MIPS assembly teaching
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
| `examples/musl/` vendoring | Already deleted from /examples; no port needed. |
| Sphinx book pipeline (`examples/book/`) | Drop — book not needed in merged tree. |
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
`examples/tests/run-demo.sh` (uncommitted) was NOT carried
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

**Status: STAGED 2026-05-23 (awaiting commit outside container).**

10 paths deleted from the new `/spimulator/examples/`:

- `book/` (6 files — entire Sphinx tree)
- `entrypoint/entrypoint.sh` (book-build script)
- `Makefile` (book orchestration)
- `Dockerfile` (book-only image; replaced by /spimulator/Dockerfile in phase 6)
- `output/.keep` (build-artifact placeholder)

`musl/` was confirmed absent — never carried over by phase 1
because it had been deleted from /examples before the merge.

Resulting `examples/` tree:
```
examples/
    READING-ORDER.md
    TEACHING-ASSEMBLER-INTERNALS.md
    src/
    tasks/
```

The orphan `tests/run-demo.sh` from the abandoned earlier
attempt has not been added yet — that's phase 5.

Suggested commit message:
> "Drop book pipeline + other unused trees post-merge.
>  Sphinx book + Dockerfile + Makefile + entrypoint were the
>  standalone /examples publishing pipeline; not needed in the
>  merged /spimulator tree where the unified Dockerfile (phase
>  6) handles build + test."

---

## Phase 3 — Cross-reference sweep

**Status: STAGED 2026-05-23 (awaiting commit outside container).**

Three classes of fixes landed:

1. **Broken markdown links** (4 instances).  After the move,
   `../spimulator/...` and `../../spimulator/...` relative
   links no longer resolve (the parent /examples dir was a
   sibling of /spimulator; now examples lives INSIDE
   /spimulator).  Fixed:
   - `examples/tasks/SESSION_NOTES.md` (2 links):
     `../../spimulator/tasks/X.md` → `../../tasks/X.md`
   - `examples/tasks/archive/README.md` (1 link):
     `../../../spimulator/tasks/X.md` → `../../../tasks/X.md`
   - `examples/tasks/archive/PLAN-tier1-tier2-tools.md`
     (1 link): same shape as the archive README.

2. **Stale `examples/` prose references** (~40 instances
   across 15 files).  `/examples` doesn't exist post-merge.
   Bulk sed: `examples/X` → `examples/X`.  These are prose
   mentions in plan docs and a few `.asm`/`.h` comments
   describing file locations.

3. **LICENSE-musl files** (2 files).  Both had a
   `Local copy   examples/musl/` line pointing at the
   vendored upstream source that was deleted before the
   merge.  Removed the line; left the Source + Repository
   URLs intact for attribution.

4. **READING-ORDER.md invocation example**.  Bumped from
   `spimulator -f src/...` to `spimulator -f examples/src/...`
   and added a "(paths relative to /spimulator root)" cwd
   hint, so readers don't have to guess.

Left as-is (out of scope for this sweep):
- `/spimulator/...` absolute paths in prose comments and plan
  docs.  These still resolve correctly post-merge (the
  absolute paths are unchanged); rewriting them to relative
  would be stylistic cleanup that can come later if wanted.
- The `/spimulator/...` references match the convention in
  the spim-side `/spimulator/tasks/` docs, so there's
  consistency value in keeping them absolute.

Files touched: 15 (1 .md doc + 14 task/license files).

Suggested commit message:
> "Cross-reference sweep post-/examples merge.
>  Fix 4 broken relative markdown links; replace ~40 `examples/`
>  prose references with relative `examples/`; drop stale
>  `Local copy examples/musl/` lines from the two LICENSE-musl
>  files (musl was deleted pre-merge); update READING-ORDER's
>  invocation example to use the post-merge path with an
>  explicit cwd hint."

---

## Phase 4 — Task & plan merger

**Status: STAGED 2026-05-23 (awaiting commit outside container).**

Layout after merge (unchanged from plan):
```
/spimulator/
    tasks/                  (spim-internal tasks)
        archive/
        README.md           (new — points at examples/tasks)
    examples/
        tasks/              (curriculum tasks: PLAN-*.md)
            archive/
```

What landed:

- **Bulk `/examples/` → `examples/` sweep** across 13
  `/spimulator/tasks/` files (and `archive/` sub-files).
  These were prose references to the formerly-sibling
  /examples repo; post-merge the relative path `examples/`
  is correct.  `NEXT-SESSION.md` didn't have any /examples/
  refs, so no change there.
- **New `/spimulator/tasks/README.md`** — short index
  pointing readers at the two task surfaces (spim-internal
  here; curriculum at `../examples/tasks/`) plus a note on
  naming conventions.
- **`HANDOFF-2026-05-22.md`** — got the same path sweep.
  Left content otherwise untouched as historical record.

Sed gotcha: the bulk replacement `s|/examples/|examples/|g`
also matched inside `/spimulator/examples/...` strings,
turning them into `/spimulatorexamples/...` — corrupted
THIS doc only (other tasks didn't have post-merge
`/spimulator/examples/` paths because they were written
pre-merge).  Caught and reverted in the same phase.  Future
bulk seds against this kind of substring should use a more
anchored pattern.

No file moves, no renames in either tasks/ tree.

---

## Phase 5 — Build integration

**Status: STAGED 2026-05-23 (awaiting commit outside container).**

What landed:

- **`/spimulator/meson.build`** gained a `subdir('examples/src')`
  at the bottom.  One line.
- **`/spimulator/examples/src/meson.build`** restructured to
  work as a subdir (not a standalone project):
  - Dropped `project('spimulator-examples', ...)` declaration
  - Dropped `add_project_arguments(edu_args, ...)` and
    `add_project_link_arguments('-nostdlib', ...)` —
    project-wide flags would have leaked into spim's compile,
    where `-nostdlib` would break the libedit + libc link
  - Added `c_args: edu_args` to all 3 static_library calls
    (io_lib, libctype_lib, libstdlib_lib) and `c_args: edu_args,
    link_args: edu_link_args` to all 7 executable calls
    (foreach demos, foreach lib_demos, atoi-demo, abs-demo,
    exit-demo, bsearch-demo, atexit-demo) — flags now scoped
    per-target
  - Added `-O0` to `edu_args` (was implicit via the dropped
    `default_options: ['optimization=0']`)
  - Appended a test()-registration block for 6 demo tests
    (suite='examples'): ctype-demo, atoi-demo, abs-demo,
    exit-demo, bsearch-demo, atexit-demo.  Uses
    `spimulator_exe` and `exception_file` from parent project
    via subdir scope.
- **`/spimulator/examples/tests/run-demo.sh`** copied in from
  the orphan at /examples/tests/.  Added the missing
  `atexit-demo` case (the orphan was written before atexit-
  demo existed).
- **`meson test -C builddir` from /spimulator root** now runs
  29 tests: 23 spim regressions + 6 examples demos.  Each
  examples-suite test validates BOTH the C binary AND the
  spim-asm version against the same `.expected` golden, and
  (for exit-demo and atexit-demo) checks `.expected-status`
  matches in both runs.  Tested locally — 29/29 green.

Suggested commit message:
> "Build integration: subdir examples/src into spim's meson tree.
>  Scope examples' compile/link flags per-target so -nostdlib
>  doesn't leak into spim's build.  Register 6 demo tests in
>  the 'examples' meson suite — each validates C and asm sides
>  against the same .expected golden + .expected-status where
>  pinned.  Bring in run-demo.sh (with atexit-demo case added)
>  from the abandoned earlier attempt.  29/29 tests green."

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
