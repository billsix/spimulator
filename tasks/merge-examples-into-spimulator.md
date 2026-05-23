# Merge /examples into /spimulator

## Goal

Pull `/examples/` (the paired C + MIPS assembly teaching
curriculum) into `/spimulator/` as a subdirectory, then unify
the build / test / container story so:

- One Dockerfile builds spim AND the example demos AND runs the
  full test suite (spim's regression tests + the
  example-demo-pair tests) — the image build FAILS if anything
  drifts.
- `meson test -C builddir` from the spim repo root runs every
  test, including the C-vs-asm goldens for the library demos
  (libctype, libstdlib, future libstr).
- The example .asm files can `-f`-load spim's library .asm
  files using relative paths within the same repo, no more
  `/spimulator/...` absolute references in /examples/ docs.

## Why

- `/examples` already depends on `/spimulator` (every demo's
  asm side can only run under spim).
- `/spimulator` already depends on `/examples` (most of the
  Unix-process conformance fixes, the multi-`-f` CLI work,
  the octal-escape bug, etc. were driven by example demos
  surfacing real-world papercuts).
- Today the test wiring across the two is manual: spim has
  meson tests; examples has none.  The library port
  (libctype + libstdlib + planned libstr) has accumulated 5
  paired demos with pinned `.expected` files but no automated
  pipeline to enforce them.
- One Dockerfile + one `meson test` is much easier to keep
  honest than two coordinated builds.

## Why now

This task is filed 2026-05-23 after a failed first pass at
building separate test infrastructure inside `/examples/` (a
`tests/run-demo.sh` runner + per-demo `test()` entries in
`/examples/src/meson.build`).  The infra worked (5/5 green
locally) but Bill flagged that combining repos is the cleaner
direction.  The orphan `/examples/tests/run-demo.sh` from
that pass should be folded into the merged setup; the script
already encodes the right per-demo invocation patterns
(libctype + libstdlib load order, exit-status verification
for exit-demo, etc.).

## Proposed shape (sketch — refine on landing)

1. **Copy** `/examples/{src, book, entrypoint, tests, tasks,
   READING-ORDER.md, TEACHING-ASSEMBLER-INTERNALS.md, …}` into
   `/spimulator/examples/`.  (Decide: hard copy, git subtree,
   or symlink during transition — see Open questions.)
2. **Update internal cross-refs.**  Files moved one level
   deeper, so `../spimulator/...` references become
   `..` (sibling-dir).  Run a sweep for `/examples/`,
   `/spimulator/`, `../spimulator/`, and similar bare absolute
   paths.
3. **Merge meson.build.**  The example demos become additional
   subdirectories of the spim build:
   - `subdir('examples/src')` in the top-level
     `/spimulator/meson.build`, OR
   - keep `/spimulator/examples/src/meson.build` as its own
     project pulled in via `meson.add_install_script` or a
     subprojects-style include.
   The library-demo tests defined in the orphaned
   `/examples/tests/run-demo.sh` get registered as
   `test()` entries.
4. **Update spim's `tests/run-test.sh`** to know about the
   library-demo tests, OR add a separate
   `examples/tests/run-demo.sh` (the existing orphan) that
   spim's meson.build invokes for those tests specifically.
5. **Rewrite the Dockerfile.**  Take spim's existing Fedora-44
   Dockerfile (or examples' Sphinx-aware one — see Open
   questions) and combine into one that:
   - installs gcc, meson, ninja, flex, bison, libedit-devel,
     diffutils (already in spim's)
   - PLUS: sphinx, texlive, aspell (currently in examples'
     Dockerfile for the book build)
   - PLUS: maybe `make` for the book makefile
   - builds spim first (so the example asm tests can find it
     at `/usr/local/bin/spimulator` after install)
   - then builds the example demos
   - then runs `meson test -C builddir` — image build FAILS
     if anything drifts.

## Open questions (decide on landing)

- **Does the standalone /examples repo persist?**  If yes (as a
  mirror or for separate publication), the merge becomes a
  one-way copy that's re-synced periodically.  If no, /examples
  becomes the deprecated home and everything moves.
- **What about `/examples/musl/`?**  Vendored upstream source,
  ~10 MB, used as reference for the libc port.  Options: copy
  in (keeps everything together); .gitignore + document how
  to fetch (smaller commits); add as a git submodule pointing
  at upstream musl.
- **What about `/examples/book/` and the Sphinx pipeline?**
  Currently the examples Dockerfile's only job is `make html`
  in /book/.  After merge: does the book still build in the
  same image, or split into a separate documentation-build
  pass?
- **Path within /spimulator/:**  `/spimulator/examples/`
  (literal mirror) vs. `/spimulator/curriculum/` (semantic
  rename) vs. `/spimulator/teaching/` (broader umbrella).
  Bill's call.
- **Multi-`-f` paths become relative.**  Currently invocations
  look like `spimulator -f /spimulator/.../libctype.asm -f ...`
  in /examples docs.  After merge, with a sibling examples/
  dir, these become `spimulator -f examples/src/lib/...` or
  similar.  Cleaner; update the docs.

## Out of scope

- Any code refactor of spim itself or the library implementations.
  This is purely a layout + build-pipeline change.
- New example demos.  The libstr libraries that aren't done
  yet can land before OR after this merge; either way works.

## When to do this

After libstdlib's last open function (`atexit + exit`, filed at
`/examples/tasks/PLAN-libstdlib-atexit.md`) is landed.  No
point reorganizing while function-by-function work is still
churning the file tree.

If libstr starts first, do the merge between libstdlib
finishing and libstr starting — fewest files in flight.

## Status

Not started.  Filed 2026-05-23.  Estimated effort: ~1 day
(physical copy + meson surgery + Dockerfile rewrite +
cross-reference sweep + verifying everything still passes).
Open questions above need answers first; some are
architectural (musl vendoring, book pipeline) and worth
deciding before touching code.
