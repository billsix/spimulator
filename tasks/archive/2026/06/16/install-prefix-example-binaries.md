# Prefix installed example binaries so they don't shadow coreutils

## Status — DONE (2026-06-16)

## Problem

`examples/src/meson.build` declared every curriculum demo `executable(..., install:
true)`, and the image builds with `meson setup --prefix=${SPIM_PREFIX}` where
`SPIM_PREFIX=/usr/local` (`Dockerfile:24`). So `meson install` (`Dockerfile:114`)
dropped ~50 demo binaries — many with **bare coreutil names** (`cp`, `cat`, `head`,
`tail`, `wc`, `tr`, `cut`, `tee`, `seq`, `comm`, `od`, `nl`, `uniq`, `tac`, …) — into
`/usr/local/bin`. Because `PATH=/usr/local/bin:/usr/bin`, the teaching demos
**shadowed GNU coreutils** inside the dev container. Any in-container tooling that
assumed GNU behavior (`cp -a`, `head -N`, …) silently broke — including a
verification script during this session.

(The `make -C examples/src all` native build at `Dockerfile:140` is unrelated — it
only populates `examples/src/bin/`, which is off PATH.)

## Fix

- `examples/src/meson.build` — prefix every demo `executable()` target name with
  `spimulator-example-` (both `foreach` loops + the 5 individual libstdlib-demo
  targets). The `spimulator` binary itself is unchanged.
- `examples/tests/run-demo.sh:97` — the one place a demo's build-dir binary is run
  by name; now `"$C_BIN_DIR/spimulator-example-$NAME"`. Only the six `*-demo`
  tests use this path; the coreutil-named demos aren't tested through it.

## Verification (in-container)

- `make image` green; `meson test` **29/29** (incl. all 6 example tests via the
  updated `run-demo.sh`).
- In the built image: `cp`/`head`/`tail`/`wc`/`cat`/`tr`/`cut` resolve to
  `/usr/bin` again, `cp -a` and `head -5` work, `/usr/local/bin` holds 50
  `spimulator-example-*` binaries and no bare coreutil names.

## Notes

- Left `examples/src/Makefile` and its `bin/` names alone (off PATH, intentional
  teaching artifact names).
