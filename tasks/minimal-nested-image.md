# Lean image for nested-podman builds — what "minimal" means for spimulator

**Status:** proposed — research done 2026-09-10 (survey of the Dockerfile + Makefile from the
runClaudeInContainer sandbox); **implementation needs go-ahead**. One of the per-project children of
runClaudeInContainer `tasks/minimal-image-for-nested-podman-standard.md` (the convention: every optional-feature build flag defaults to its lean value when
`NESTED_PODMAN=1`); the fleet-wide findings table is runClaudeInContainer `tasks/reference/minimal-nested-images.md`. Created 2026-09-10 at the maintainer's
request (William Emerison Six <billsix@gmail.com>: "go through all of my projects with CLAUDE.md …
research what a minimal nested podman container would be for them").
**Priority:** 4
**Difficulty:** 3

## BLUF

Make `make image` inside a sandbox (which exports `NESTED_PODMAN=1`) build a lean image that fits
the nested RAM store and still runs this project's gate, while a host `make image` stays
byte-identical — via the idiom `FLAG ?= $(if $(filter 1,$(NESTED_PODMAN)),0,1)` on each optional-feature flag (the `PODMAN_RUN_FLAGS`
pattern applied to build flags; reference implementation: runCrushInContainer `client/Makefile`,
`FULL_TOOLCHAIN`). Done = the flags below carry the nested-aware default, a nested `make image`
builds and passes the gate, both image sizes are measured and recorded here and in `CLAUDE.md`.

## Context — read first

- runClaudeInContainer `tasks/reference/minimal-nested-images.md` — the standard, the idiom, the rules (a project's *gates* and *product build deps* are never
  trimmed; only editors, docs toolchains, notebooks, GUI extras), and every project's row.
- This repo's `Dockerfile`, `Makefile` (flag block + `image` target), `entrypoint/*install*.sh`.
- The flag-coverage rule (cross-project `CLAUDE.md` › "Verification gates in nested containers"): a
  lean build verifies nothing about the layers it skips — when a change touches what a skipped layer
  consumes, build with that flag ON.

## Findings (2026-09-10)

**What the image installs today.** Four flags, all Makefile-default 1: `USE_EMACS`, `BUILD_TREE_SITTER` (npm + the grammar build), `BUILD_DOCS` (TeX + sphinx + inkscape), `RUN_SANITIZERS` (UBSan/ASan regression suites at image time). `01-install-base.sh` is 14 lean packages (clang, meson, valgrind, gdb, lldb…); the meson build + `meson test` run at image time.

**What "minimal" is here.** Nested default: `USE_EMACS=0`, `BUILD_DOCS=0`. **`BUILD_TREE_SITTER` and `RUN_SANITIZERS` stay 1** — they are gates, and the 2026-07-07 lesson (an `opcodes.h` rename sailed through three `BUILD_TREE_SITTER=0` gates) is exactly why the lean variant must not switch them off by default.

**Notes.** Sanitizer suites under fuse-overlayfs can trip fixed timeouts (nested-podman-design lore); `RUN_SANITIZERS=0` stays the documented opt-out, never the default.

## Plan

- [ ] Makefile: `USE_EMACS` and `BUILD_DOCS` get the nested-aware default; `BUILD_TREE_SITTER` and `RUN_SANITIZERS` keep `?= 1` with a comment saying why they are exempt.
- [ ] Measure both images; `CLAUDE.md` (the gate paragraph gains the nested default and the exemption).
- [ ] A docs-touching change still needs `BUILD_DOCS=1` nested — already the repo's rule.
- [ ] Record both sizes (host full vs nested lean) here and in `CLAUDE.md`; add the standard's one-line
      rule to `CLAUDE.md` ("nested = lean image automatically; `FLAG=1` overrides").

## Open questions

None — the standard's decisions (dnf-only, gates never trimmed) were the maintainer's on 2026-09-10;
anything project-specific to decide is flagged inline above.
