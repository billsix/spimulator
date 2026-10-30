# Give the `wc` demo golden-test coverage

**Status:** done 2026-10-30 (William Emerison Six <billsix@gmail.com>) — verified
standalone (`meson test --suite examples` 34/34, `wc` OK on both C and asm) and
in the containerized `make image` gate (examples suite 59/59 with `wc` OK both
sides; UBSan 25/25; ASan 25/25; image tagged). Staged for the maintainer's
commit; archives on its own commit after the work commit.
**Priority:** 6
**Difficulty:** 3

## BLUF

`examples/src/transforms/wc/wc.{c,asm}` was built by `meson.build` but had **no
golden test** — the lone counting demo `examples/tests/run-demo.sh` never
exercised, so a regression on either the C side or the spim-asm side would have
passed CI unnoticed. This task pins its output with a fixed-input golden
(`wc.input` / `wc.expected`), wires it into the examples suite, and verifies both
the native binary and the asm-under-spim match. **Done** = `wc` runs in
`run-demo.sh` on both sides against a golden computed from the system `wc`
oracle, and the containerized `make image` gate is green. **(Done.)**

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

## What was done

1. **Fixed input** `examples/src/transforms/wc/wc.input` =
   `hello world\nfoo bar\nbaz\nno newline here` — three newlines plus a final
   line with **no trailing newline**, the chosen edge: `byte_count` counts that
   line, `line_count` (newlines only, matching `wc -l`) does not.
2. **Golden** `wc.expected` = `39 bytes, 3 lines`, computed from the oracle
   (`wc -c`=39, `wc -l`=3 on that input). The demo's `line_count` and `wc -l`
   agree on the no-trailing-newline case (both count `\n` characters).
3. **Wiring:** added `'wc'` to the `lib_demo_tests` golden list in
   `examples/src/meson.build` (it was already in the build list) and a `wc)` case
   block in `examples/tests/run-demo.sh` (stdin path, piping `wc.input` to both
   the C binary and spim). No `examples/src/Makefile` edit (it globs `*.c`); no
   `READING-ORDER.md` change (the existing `wc` entry needed none).

## Verification

- Standalone: `meson test -C builddir --suite examples` → 34/34 OK (was 33; `wc`
  is the +1), and the `wc` test passed on **both** the C binary and the
  spim-asm side (`run-demo.sh` byte-diffs each against `wc.expected`).
- Containerized `make image` gate (nested): examples suite **59/59** (`wc` OK on
  both sides), UBSan-trap regression **25/25**, ASan regression **25/25**,
  `Successfully tagged localhost/spimulator:latest`. (One earlier run hit a
  transient 30s TIMEOUT on the UBSan `core` regression test — nested-container
  slowness under instrumentation, unrelated to this examples-only change, which
  the sanitizer gate never touches; a re-run was clean.)

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
