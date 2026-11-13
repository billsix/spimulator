# GitHub Actions CI: check formatting and run the tests on every push, driven by make

**Status:** proposed — needs go-ahead (questions answered 2026-11-13)
**Priority:** 5
**Difficulty:** 4
**Created:** 2026-11-13 (William Emerison Six <billsix@gmail.com>)

## BLUF

Add `.github/workflows/checks.yml` so that every push and pull request to GitHub fails when the code
is not formatted or when the test suite fails. "Formatted" covers the C engine (clang-format) and the
three engine ports: Python (`ruff format`), Rust (`cargo fmt`) and Haskell (ormolu). As in github.com/billsix/geometricalgebra and
github.com/billsix/modelviewprojection, each job is only `checkout` followed by `make <target>`, and
all the logic lives in the Makefile, so the same command runs on a laptop. Done means:
- the workflow exists with a `format` job and a `test` job;
- both jobs pass on a GitHub `ubuntu-latest` runner (Docker, not podman);
- `make check-format` and the test target also run locally.

## Context

- **The two reference workflows** (read both first):
  - geometricalgebra `.github/workflows/checks.yml`: jobs `format` (`make check-format
    MINIMAL_IMAGE=1`), `test`, `generated` and `notebooks`. Its header comment states the
    thin-wrapper rule. Its `check-format` target is two lines: `check-format: format` then
    `git diff --exit-code`.
  - modelviewprojection `.github/workflows/checks.yml`: the same shape. Its `CLAUDE.md` ("Continuous
    integration") explains why.
  - Both pin `actions/checkout@v5` (Node 24; `@v4` uses the deprecated Node 20) and build the lean
    image on the runner.
- **spimulator today** has no `.github/` directory.
  - `CONTAINER_CMD` already falls back from podman to docker (Makefile line `CONTAINER_CMD ?=`).
  - `make format` runs `entrypoint/format.sh`, which is `clang-format -i` over every
    `*.c`/`*.cpp`/`*.h`/`*.hpp` except `pgu/upstreamSource`.
  - **The tests already gate the image build.** `CLAUDE.md` ("Build / container workflow") calls
    `make image` the gate:
    - its preprocessing stage fails on stale generated opcode tables (`tools/gen_opcodes.py --check`);
    - `RUN meson test -C ${SPIM_BUILD_DIR} --print-errorlogs` runs every meson suite (regression,
      port-python, port-haskell, port-rust, examples).

    So `make image` fails if any test fails. There is no separate `make test` target. A
    `RUN_SANITIZERS=1` build (Makefile default 1) also runs the regression suite under UBSan and ASan.
- **What breaks on a GitHub runner** (found by reading the Makefile on 2026-11-13; confirm each on
  the first CI run):
  1. `format` runs `podman run … -it`. On a runner there is no TTY, so Docker fails with "the input
     device is not a TTY". `tasks/archive/2026/11/13/remove-tty-flag-from-batch-make-targets.md` removed `-it` from
     `format` and the other batch targets. Do it first; then `check-format: format` works on a
     runner as is.
  2. `FILES_TO_MOUNT` uses a relative host path, `-v .:/spimulator/:Z`. Recent Docker accepts
     relative bind paths, but older Docker reads `.` as a volume name. If the runner's Docker
     rejects it, use `$(CURDIR)`.
  3. `make image` passes `$(ELPA_MOUNT)` (a `-v`) to `build`. That works on podman but `docker build`
     has no `-v`. It is empty when `USE_EMACS=0`, so a lean CI build avoids it, but only if the lean
     build turns Emacs off (item 4).
  4. **No `MINIMAL_IMAGE` idiom.** The Makefile's `USE_EMACS ?= 1`, `BUILD_TREE_SITTER ?= 1` and
     `BUILD_DOCS ?= 1` are plain defaults. Following the fleet convention
     (runClaudeInContainer `tasks/reference/minimal-nested-images.md`), the flags a lean image drops
     become `?= $(if $(filter 1,$(MINIMAL_IMAGE)),0,1)`:
     - `USE_EMACS` and `BUILD_DOCS`.

     A flag the gate needs stays plain `?= 1`. That includes **`BUILD_TREE_SITTER`**: `CLAUDE.md`
     says any `opcodes.tsv` change must be gated with it, because a `BUILD_TREE_SITTER=0` image never
     builds or tests the grammar, so a CI that dropped it would miss a broken grammar.
     `RUN_SANITIZERS` is also a gate flag (Decisions 2).
  5. **Drift found along the way:** the Dockerfile declares `ARG BUILD_DOCS=1`. The fleet convention
     is `0` in the Dockerfile and `1` in the Makefile, so a bare `podman build` is lean. Fix it in
     the same change, since the CI image build depends on what the defaults mean.
- **Every containerized target a CI job runs needs `: image`** as a prerequisite, because a fresh
  runner has no image (`format: image` already has it).
- **`make format` must also format the ports** (William Emerison Six <billsix@gmail.com>, 2026-11-13).
  Today `entrypoint/format.sh` runs only clang-format. The ports' tools are already in the image
  (`entrypoint/05-install-ports.sh` installs `ruff`, `ty`, `hlint`, `rustfmt` and `clippy`), but
  only in check mode, inside meson's lint suites:
  - `port/python/lint.sh` runs `ruff check .` and `ruff format --check .`;
  - `port/rust/lint.sh` runs `cargo fmt --check`;
  - `port/haskell/lint.sh` runs only `hlint` (advisory). No Haskell formatter is installed.

  Measured 2026-11-13 in the sandbox:
  - Python: `ruff format --check` reports all 26 files already formatted;
  - Rust: `cargo fmt --check` exits 0;
  - Haskell: `ormolu --mode check` (ormolu 0.7.7.0, the Fedora 45 package) flags **24 of the 26**
    `.hs` files under `src/`, `tools/` and `tests/`. ormolu is the formatter the shared Haskell
    standard names (the haskell-coding-standard.md reference doc in
    github.com/billsix/runClaudeInContainer).

  The first ormolu run is therefore a whole-tree reformat of the Haskell port. Give it its own commit
  and list that commit in a `.git-blame-ignore-revs` (the cpp-build-modernization.md reference doc in
  the same repo explains the mechanism). Then rerun the port suites: the reformat must change
  layout only, so `port-haskell` stays green.

## Plan

1. Add the `MINIMAL_IMAGE` idiom (item 4) and fix the Dockerfile `ARG BUILD_DOCS` default (item 5).
   Check that `make image MINIMAL_IMAGE=1` builds and still runs `meson test`.
2. Make the format path TTY-free (item 1) and add `check-format: format` + `git diff --exit-code`,
   `## `-documented, as geometricalgebra does.
3. Extend `entrypoint/format.sh` to the ports. Keep the multi-step shape, `status=0; cmd ||
   status=1; … exit $status`, so every formatter runs and any failure fails the script:
   - `ruff check --fix` and `ruff format` in `port/python`;
   - `cargo fmt` for `port/rust/Cargo.toml`;
   - `ormolu --mode inplace` over `port/haskell`'s `src/`, `tools/` and `tests/` `.hs` files.

   Add `ormolu` to `entrypoint/05-install-ports.sh`. Do the first Haskell reformat as its own commit,
   added to `.git-blame-ignore-revs` (Context). Optionally add `ormolu --mode check` to
   `port/haskell/lint.sh`, so the image build also catches unformatted Haskell; `check-format` already
   catches it in CI.
4. Write `.github/workflows/checks.yml` with a header comment stating the thin-wrapper rule (copy
   geometricalgebra's wording, adapted). Jobs:
   - `format` runs `make check-format MINIMAL_IMAGE=1`;
   - `test` runs `make image MINIMAL_IMAGE=1` with `RUN_SANITIZERS=1` (the default), which runs every
     meson suite, the generated-tables check and the sanitizer gate (Decisions).

   Both use `actions/checkout@v5` and run on `push` and `pull_request`.
5. Verify:
   - locally, with nested podman and the same commands the workflow runs;
   - on GitHub, once the maintainer pushes. Only the maintainer can push. Fix items 2 and 3 if the
     runner's Docker trips on them.
6. Record CI in `CLAUDE.md` and, if it has a commands section, the README: the jobs, and the rule
   that CI is `make` targets only.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-13)

1. **The CI `test` job runs `make image MINIMAL_IMAGE=1`.** No new `make test` target. The image
   build is already the documented gate: it checks the generated opcode tables, runs every meson
   suite, and fails on any failure. A fresh runner has no layer cache, so the tests always run.
   (The rejected alternative was a `make test: image` that reran `meson test` on the bind-mounted
   tree; it would have run the whole suite twice in CI and skipped the generated-tables check.)
2. **CI runs the sanitizer gate** (`RUN_SANITIZERS=1`, the Makefile default) in that same image
   build. Sanitizer failures are what CI should catch, and the cost is one more regression-suite
   run.
3. **`make format` formats the ports too**: Python, Rust and Haskell, as in Plan step 3.
