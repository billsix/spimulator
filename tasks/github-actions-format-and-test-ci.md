# GitHub Actions CI: check formatting and run the tests on every push, driven by make

**Status:** proposed — needs go-ahead (and answers to the Open questions)
**Priority:** 5
**Difficulty:** 3
**Created:** 2026-11-13 (William Emerison Six <billsix@gmail.com>)

## BLUF

Add `.github/workflows/checks.yml` so that every push and pull request to GitHub fails when the C code
is not clang-formatted or when the test suite fails. As in github.com/billsix/geometricalgebra and
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
     device is not a TTY". The CI path needs a run without `-it`. Either add a `check-format` target
     that hand-rolls a minimal `run` (geometricalgebra's `CLAUDE.md`, "Continuous integration",
     explains why CI targets avoid the interactive flags), or drop `-it` from `format`, which does
     not need a TTY.
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
     `RUN_SANITIZERS` is also a gate flag (open question 2).
  5. **Drift found along the way:** the Dockerfile declares `ARG BUILD_DOCS=1`. The fleet convention
     is `0` in the Dockerfile and `1` in the Makefile, so a bare `podman build` is lean. Fix it in
     the same change, since the CI image build depends on what the defaults mean.
- **Every containerized target a CI job runs needs `: image`** as a prerequisite, because a fresh
  runner has no image (`format: image` already has it).

## Plan

1. Add the `MINIMAL_IMAGE` idiom (item 4) and fix the Dockerfile `ARG BUILD_DOCS` default (item 5).
   Check that `make image MINIMAL_IMAGE=1` builds and still runs `meson test`.
2. Make the format path TTY-free (item 1) and add `check-format: format` + `git diff --exit-code`,
   `## `-documented, as geometricalgebra does.
3. Add the test target chosen in open question 1.
4. Write `.github/workflows/checks.yml` with a header comment stating the thin-wrapper rule (copy
   geometricalgebra's wording, adapted). Jobs:
   - `format` runs `make check-format MINIMAL_IMAGE=1`;
   - `test` runs the test target with `MINIMAL_IMAGE=1`, plus `RUN_SANITIZERS` per open question 2.

   Both use `actions/checkout@v5` and run on `push` and `pull_request`.
5. Verify:
   - locally, with nested podman and the same commands the workflow runs;
   - on GitHub, once the maintainer pushes. Only the maintainer can push. Fix items 2 and 3 if the
     runner's Docker trips on them.
6. Record CI in `CLAUDE.md` and, if it has a commands section, the README: the jobs, and the rule
   that CI is `make` targets only.

## Open questions

1. Which test target should the CI `test` job run?
   - **(a)** `make image MINIMAL_IMAGE=1`, since the image build already runs `meson test` and fails
     on a failure. That needs no new target, and a fresh runner has no layer cache, so the tests
     always run.
   - **(b)** A new `make test: image` that runs `meson setup`, `meson compile` (every target, which
     `CLAUDE.md` "Tests" requires because the examples' and ports' tests compare against the demos'
     native binaries) and `meson test` in the container against the bind-mounted tree, with the
     build directory outside the mount. That also lets you re-test host edits locally without an
     image rebuild, but CI then runs the whole suite twice (once in the image build, once in the
     target), including the three port suites.

   Recommendation: (a) for CI, because the image build is already the documented gate and runs the
   generated-tables check that (b) would skip. Add (b) only if you also want it as a local command.
2. Should CI also run the UBSan/ASan pass (`RUN_SANITIZERS=1`, the Makefile default)? Recommendation:
   yes, in the `test` job's image build. Sanitizer failures are the kind CI should catch, and the cost
   is one more regression-suite run.
