# Give the `wc` demo golden-test coverage

**Status:** proposed — needs go-ahead
**Priority:** 6
**Difficulty:** 3

## BLUF

`examples/src/transforms/wc/wc.{c,asm}` is built by `meson.build` but has **no
golden test** — it is the lone counting demo that `examples/tests/run-demo.sh`
never exercises, so a regression on either the C side or the spim-asm side would
pass CI unnoticed. This task pins its output with a fixed-input golden
(`wc.input` / `wc.expected`), wires it into the examples suite, and verifies both
the native binary and the asm-under-spim match. **Done** = `wc` runs in
`run-demo.sh` on both sides against a golden computed from the system `wc`
oracle, and the containerized `make image` gate is green.

This is **test coverage, not I/O separation.** `wc` was deliberately left whole
in the 2026-10-30 remaining-demos I/O-split batch: its "algorithm" is a
two-counter byte loop with no nontrivial pure core to factor out (same call as
the per-byte-map filters `tr`/`rot13`/`yes`). The separate reason it was skipped
there — "it has no golden baseline either, so doing it well is its own task" — is
exactly this task. Do **not** reopen the I/O-separation question for `wc`; see
`tasks/reference/incremental-example-decomposition.md` §"Separate only where a
nontrivial pure core actually hides" (and the archived batch doc
`tasks/archive/2026/10/30/separate-io-from-algorithm-remaining-demos.md`).

## Context

`wc.c` is a port of `sbase/wc -cl`: it reads stdin (or `wc -`, or `wc FILE`),
counts bytes and lines in one `os_read`-per-byte loop, and prints

    <bytes> bytes, <lines> lines

(a deliberately non-standard format — not real `wc`'s columns — so the golden is
computed from the counts, not from `wc`'s own stdout). `my_main` also handles
argv: `>2` args or a bad arg is a usage error, a single non-`-` arg is opened as
a file. The byte/line loop is the lesson; there is no hidden pure function, which
is why it stays a single whole-program demo.

Wiring reference — the established shape (any recent golden-tested demo, e.g.
`cksum`/`head` which already pair a `.input` with a `.expected`):

- `examples/src/meson.build` — `wc` is **already in the build list** (the
  `['wc', 'transforms/wc/wc.c']` entry); it is **missing from the `lib_demo_tests`
  golden list**. Add it there.
- `examples/tests/run-demo.sh` — add a `wc)` case block that sets
  `expected=$SRC_DIR/transforms/wc/wc.expected` and
  `input=$SRC_DIR/transforms/wc/wc.input` (model it on the `head`/`cksum` stdin
  cases, which pipe `.input` to both the C binary and spim).
- `examples/src/Makefile` — **no edit** (it globs `*.c`; no new lib/helper).
- `examples/READING-ORDER.md` — `wc` already has an entry; no new demo is added,
  so at most a one-line "golden-tested on a fixed input" touch if the existing
  entry implies otherwise (check; likely nothing to change).

## Plan

1. **Pick a fixed input** that exercises both counters (multiple lines, a line
   without a trailing newline is a good edge — it still counts bytes but adds no
   line). Write it to `wc.input`.
2. **Compute the golden from the oracle** and write `wc.expected`:
   `bytes=$(wc -c < wc.input)`, `lines=$(wc -l < wc.input)`, so the file holds
   `<bytes> bytes, <lines> lines`. Worked example: input `hello world\nfoo\nbar\n`
   → `wc -c`=20, `wc -l`=3 → golden line `20 bytes, 3 lines`.
   (Note `wc -l` counts newline *characters*, which equals `line_count` in `wc.c`;
   if the chosen input ends without a newline, hand-check that the demo's count
   and `wc -l` still agree.)
3. **Wire** the three files above (meson golden list + run-demo.sh case; no
   Makefile/reading-order change expected).
4. **Verify** standalone: `meson test --suite examples` — the new `wc` test
   registers and passes on **both** the C binary and the spim asm; byte-diff each
   built binary's stdout against `wc.expected`.
5. **Gate:** containerized `make image` green (examples suite count +1, both
   sanitizer runs unaffected, native `.s`+`bin` for `wc` already built).

## Scope / decisions

- **In scope:** the stdin path only — one fixed `.input`, pinned golden, both
  sides. This is the whole value: it turns an untested demo into a tested one.
- **Out of scope (note, don't do without a separate decision):** golden-testing
  the **argv paths** — `wc FILE` (needs a file materialized in the container +
  spim argv, which `run-demo.sh`'s stdin-piping harness doesn't set up) and the
  **usage-error** path (`>2` args → exit 1 + message, which would need an
  `.expected-status` like the `exit-demo`/`atexit-demo` cases). These are a
  distinct, harder rung ("peel off where the input comes from" / argv coverage in
  the decomposition reference doc); if wanted, file them separately rather than
  stretching this task.

## Follow-ups

- None required. If the argv/FILE and usage-error paths are later wanted, they are
  their own small task (see Scope above).
