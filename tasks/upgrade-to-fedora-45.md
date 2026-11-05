# Upgrade the container base from Fedora 44 to Fedora 45

**Status:** in progress
**Priority:** 3
**Difficulty:** 4

## BLUF

Bump this repo's two Dockerfiles (`Dockerfile`, `pgu/Dockerfile`) from
`registry.fedoraproject.org/fedora:44` to `fedora:45` and get `make image` green
nested (build + `meson test` + the UBSan/ASan sanitizer gate, which is on by
default). "Done" = the image builds and all build-time gates pass on the 45 base.

## Context (read first)

Part of a fleet-wide Fedora 44 → 45 bump (William Emerison Six
<billsix@gmail.com>, 2026-11-05). Fedora 45 (released ~2026-11-03) ships **GCC
16.2 / clang from LLVM 23, glibc 2.44, Python 3.15**. The real risk here is the
**toolchain bump**: spimulator's core builds under `-Werror`
(`-Dcore_werror`, default on) and `-Wconversion -Wsign-conversion`, and `make
image` additionally runs the UBSan-trap + ASan gate (`RUN_SANITIZERS=1`
default). A new GCC/clang major can surface new warnings that fail `-Werror`, or
new UB diagnostics. The docs toolchain (`BUILD_DOCS=1`, TeX Live + Sphinx) is a
secondary risk (version drift only).

- Build gate: `make image` (nested; `PODMAN_RUN_FLAGS` auto-adds
  `--cgroups=disabled` from the sandbox's `NESTED_PODMAN=1`).
- Base image bump only — no source changes expected unless the new toolchain
  forces a warning/UB fix.

## Plan

1. `Dockerfile` + `pgu/Dockerfile`: `fedora:44` → `fedora:45`.
2. `make image` nested; read the tail / `--print-errorlogs` on failure.
3. On a toolchain failure, research the specific GCC 16 / clang 20+ diagnostic
   and fix minimally (prefer a scoped fix over disabling the gate).
4. Re-run until green; stage by path; archive.

## Notes / findings

(appended as the build runs)
