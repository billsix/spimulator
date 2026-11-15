# `make image` failed on some machines: port builds and heavy checks ran out of test time

**Status:** complete
**Completed:** 2026-11-15
**Priority:** 2
**Difficulty:** 2
**Created:** 2026-11-15 (William Emerison Six <billsix@gmail.com>)

## What this did

`make image` failed at the `meson test` step on some machines (including the 24-core sandbox)
while passing on others and in GitHub CI. Two causes were fixed:

1. **A compiled port built itself inside a timed test.** The Haskell and Rust launchers compile
   their binary on first use. Nothing built them before `meson test`, so the first test to call a
   launcher paid for the compile. When that test was one of the regression cases under
   `timeout 10` (`parse_error_data_list`, `engine_fixes`), the ghc build (about 9 s idle, more
   under load) pushed it past the limit and it failed with exit 124. A second test waiting on the
   build's `flock` failed the same way. Whether it failed depended on load and on which test meson
   started first.
2. **Two clang-bound checks had meson's 30 s default timeout.** With no compile inside the tests,
   the port suites ran together, and `dispatch_check` and `designated_init_check` (about 4 s
   each alone) ran past 30 s.

The rules this established are in `tasks/reference/engine-ports-design.md` ("How a port is
tested"); the general rule ("everything the project builds is built at image-creation time") is in
the maintainer's personal overlay (github.com/billsix/dotfiles `.ai-coding-conventions.personal.md`,
Dockerfile contract).

## What was built

- `Dockerfile`: a layer after the meson build that runs `port/haskell/build.sh` and
  `cargo build --release --offline --quiet --manifest-path port/rust/Cargo.toml`. The test layer
  runs `port/check-binaries-fresh.sh && meson test …`.
- `port/check-binaries-fresh.sh` (new, executable): fails, naming the binary, if either port
  binary is missing or older than its sources, using the same test as each launcher's `stale`.
- `meson.build`: `dispatch_check` and `designated_init_check` take `timeout: ast_check_timeout`
  (300 s), with a comment saying why.
- Docs: `port/README.md` ("Testing a port"), the `CLAUDE.md` "Tests" section,
  `tasks/reference/engine-ports-design.md`.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-15)

- Build the port binaries in the Dockerfile before `meson test`. Rejected: meson build targets
  for the ports, a meson setup step or first "build" test (meson's ordering between tests is
  weak), and taking the build out of the 10 s budget (it changes the harness instead of the
  build).
- Cover the Rust port too, although only the Haskell port failed.
- Guard the fix with a pre-test freshness assertion. A cold-build `meson test` run was rejected
  as the check, because it reproduces the original failure on purpose (the launchers still
  compile on first use).
- Raise the two AST checks' timeouts to 300 s in this task. Rejected: `is_parallel: false`
  (slower overall), parallelizing the scripts' clang calls (more work, still load-sensitive), and
  a separate task.

## Verification

- **Failure reproduced** on `master` at `5e407867`: 347 OK, 2 FAIL
  (`haskell-parse_error_data_list`, `haskell-engine_fixes`, both exit 124 at 10.1 s). Run alone,
  the first Haskell call took 9.02 s (the ghc build) and the next 0.02 s.
- **Negative test of the freshness check**, run against image layers: it exits 1 and names each
  binary when both are missing (the pre-fix layer) and when both are stale (`Main.hs` and
  `Cargo.toml` touched).
- **First fixed run** (default flags): the formerly failing cases passed in 0.4–0.6 s, but
  `dispatch_check` (31.37 s) and `designated_init_check` (31.46 s) timed out. That led to
  cause 2.
- **Default-flags `make image`** after both fixes: 349 OK, 0 FAIL, 0 TIMEOUT.
  `designated_init_check` took 45.56 s, so the raised limit was needed.
- **CI command** `make image MINIMAL_IMAGE=1 RUN_SANITIZERS=1`: 349 OK, 0 FAIL; the UBSan and
  ASan gates ran 54 OK each with no failures; `dispatch_check` 26.02 s, `designated_init_check`
  48.82 s.
- All runs were nested podman on the 24-core sandbox. Not timed on a GitHub runner.
