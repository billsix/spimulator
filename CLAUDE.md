# spimulator

A fork of **SPIM** (James Larus's MIPS R2000/R3000 assembly simulator) maintained
by William Emerison Six as a teaching tool. The core is a text-based MIPS32
interpreter; the fork adds a modern build, a worked-examples curriculum, a MIPS
port of the *Programming from the Ground Up* book, an editor grammar, and a
"teaching mode" that explains instructions as they execute.

## Status

- **Build:** Meson + Ninja (replaced the legacy Make/xmkmf). GNU C23
  (`c_std=gnu23`); the core is built `-Werror` (`-Dcore_werror`, default on; scoped to the
  `spimulator` target so `examples/` keep their own warning posture). Options:
  `-Dline_editing` (libedit REPL history; default auto), `-Dcore_werror`.
- **Parser:** hand-written recursive-descent scanner/parser (`scanner.c` /
  `parser.c`); parses to an AST, then `emit_ast` walks it to commit code. Single
  parse mode. History (flex+bison and PARSE_DIRECT removal) + design:
  `tasks/reference/parser-ast-emit.md`.
- **Teaching mode:** `-explain=N` (N 0–4; bare `-explain` = 3) narrates each executed
  instruction on stderr (`explain.c`) without changing what the program does (same stdout,
  exceptions and exit status; `explain_parity` tests it): L1 a step header (PC, encoding,
  disassembly, source line), a one-line pseudo-op/expansion note from the assembler's
  record (`mips_instruction`'s `expansion_*` fields), and the exit status; L2 adds the
  pseudo-op description, category, meaning, inputs, writes and try-it hints; L3 adds a
  bit-field box; L4 swaps it for decode frames. A learner-model redesign is tracked in
  `tasks/explain-levels-redesign.md`; target design: `tasks/reference/teaching-mode-learner-model.md`.
- The image builds + runs the full test suite at build time and fails on any
  regression.

## Layout

- `src/` — simulator: `spim.c` (REPL), `run.c` (execute), `memory.c`,
  `instruction.c` (codec / disassemble / explain), `syscall.c`, `scanner.c`,
  `parser.c`, `ast.c`, `pseudo-op.c`, `symbol-table.c`, `explain.c`;
  `exceptions.s` (the trap handler), `opcode-tables.c` (generated). `include/` —
  `opcodes.tsv` + `opcode-types.tsv` (the opcode table's source, see Conventions),
  the generated `tokens.h` / `opcode-types.h` / `opcode-tables.h`, `registers.h`,
  `spim.h`, parser/AST headers.
- `examples/` — paired **C + MIPS-asm** teaching demos (`intro/`, `algorithms/`,
  `transforms/`, `fileio/`, `arguments/`, `recursion/`, `extras/`, plus
  `lib/libctype` + `libstdlib` + `libstr` adapted from musl). `examples/src/meson.build`
  builds them; `examples/src/Makefile` materializes native `.s` listings +
  binaries (and `make debug` builds debuggable `bin-debug/` binaries with `-g`
  for gdb/lldb); `examples/tests/run-demo.sh` runs C and asm and diffs both against
  goldens. **The two build systems are independent and must be kept in sync** — the
  Makefile sets its own `-std=gnu23` (matching meson's `c_std`) and keeps its own
  helper/include/archive inventory, so adding a lib or demo to `meson.build` means
  mirroring it in the Makefile (see
  `tasks/archive/2026/10/28/fix-examples-listing-std-flag.md`). Curriculum design is in
  `examples/READING-ORDER.md` and the curriculum task docs in `tasks/`.
- `pgu/` — the *Programming from the Ground Up* book ported to MIPS/spim (its own
  `Dockerfile`/`Makefile`/`docs/`). Port **complete**
  (`tasks/archive/2026/06/14/port-pgu.md`, 2026-05-25).
- `tree-sitter/` — editor grammar; its keyword lists (`scripts/keyword-lists.js`) are generated
  from `include/opcodes.tsv`, so they stay in sync.
- `tests/` — regression suite (`run-test.sh`, `tt.*.s` / `tt.*.in`).
- `port/` — the engine ported to Python (`port/python/`), GHC Haskell (`port/haskell/`) and Rust
  (`port/rust/`), each a `spimulator-<language>` command with the C engine's flags. **The C engine is
  the standard**: every port must give the same stdout, stderr and exit status (a float printed with
  different digits is only a warning). `port/meson.build` runs the C engine's own suites against each
  port, plus `port/harness/` (case-by-case, whole-tree and REPL-transcript comparisons with C). The teaching mode is not
  ported yet. Rules and decisions: `port/README.md`, `tasks/reference/engine-ports-design.md`. **A C
  behavior change must be followed in every port**, or the port suites fail. The Haskell port follows the shared
  Haskell coding standard: every do-bind and `let`/`where` binding typed, checked by
  `port/haskell/tests/check-annotations.sh` (meson `haskell-annotations`).
- `Documentation/spimulator.1` (the man page), `meson.build`, `meson_options.txt`, `.clang-format`,
  `.clang-tidy`.

## Build / container workflow

Standalone: `meson setup builddir --buildtype=debug -Dline_editing=enabled &&
meson compile -C builddir && meson test -C builddir`.

Container (Fedora-45 + podman family template) — build args `USE_EMACS`,
`BUILD_TREE_SITTER`, `BUILD_DOCS`:

**Development goes through the Dockerfile by default:** `make image` is the gate. It checks that
the generated opcode tables are current (its preprocessing stage runs `tools/gen_opcodes.py
--check`), builds, runs every meson suite, and runs the sanitizer gate. A standalone `meson` build
is for quick iteration only.

- `make help` *(default)* — list the documented targets.
- `make image` — check the generated files, build + test spim at image-build time; build the
  examples' native artifacts; optionally build the tree-sitter grammar / Emacs integration.
- `make generate` — regenerate every opcode table from `include/opcodes.tsv`, on the existing image (not
  `generate: image`: the image build rejects stale tables, so it fails until you regenerate);
  commit the result with the data-file change (`README.md`, "Changing the opcode table").
- `make shell` *(runs `format` first)* — dev shell.
- `make format` — clang-format on the C engine, plus the ports' formatters: `ruff` (Python),
  `cargo fmt` (Rust) and `ormolu` (Haskell), all in `entrypoint/format.sh`; `lint.sh` runs
  `clang-tidy`. Both run on shell exit. `make check-format` (CI) runs `format` and fails on any
  `git diff`.
- `make image MINIMAL_IMAGE=1` — a lean image without Emacs or the book toolchain
  (`USE_EMACS`/`BUILD_DOCS` off). The gate flags `BUILD_TREE_SITTER` and `RUN_SANITIZERS` stay on,
  so it still runs every suite.
- `make html` / `pdf` / `epub` / `docs` *(BUILD_DOCS)* — build the **pgu** book via
  sphinx-build directly (the Makefile rasterizes SVGs and calls `sphinx-build`
  rather than `pgu/docs/Makefile`, which routes through an interactive aspell step).

## Tests

Meson suites: **regression** (assembler, syscalls, exceptions,
teaching-mode goldens, AST parity; `tests/run-test.sh`), **port-python**, **port-haskell**,
**port-rust** (those suites again against each port, plus the comparisons; `port/meson.build`),
and **examples** (each demo
runs both the C binary and the spim asm, diffing stdout + exit status against
pinned goldens; `examples/tests/run-demo.sh`; plus `documented-commands`, which runs every demo's
BUILD AND RUN header commands verbatim, `tools/check_example_commands.py`).
Running them from a standalone builddir needs every target built (`meson compile -C builddir`,
not just the `spimulator` target): the examples' and the ports' demo tests compare against the
demos' native C binaries, and fail when those are missing.

**Sanitizer gate** (`RUN_SANITIZERS=1`, the `make image` default; `make image
RUN_SANITIZERS=0` to skip): the image build also compiles **spim only** (the
`spimulator` target — the `-nostdlib` demos must not be sanitized) under
**UBSan-trap** (`-fsanitize=undefined -fsanitize-trap=undefined`) and **ASan**
(`-Db_sanitize=address`) and runs the regression suite under each, failing the
image on any UB or memory error. Rationale (trap mode over diagnostic, which
under-reports; ASan leak detection defaulted off so the gate targets corruption,
not spim's intentional exit-time leaks) + the integer-UB primer:
`tasks/archive/2026/06/16/ubsan-sweep.md`.

## Conventions

- **Generated sources are committed, never edited by hand.** A file a tool generates from another
  file in this repo is checked in next to its source. Upstream SPIM did the same with its flex/bison
  output until `ebe63b4` (2025-11-07) moved that step into the build. Committing it means the build
  and the image need no generator toolchain, and a change to the source shows its effect in the
  diff. After changing the source, run the generator and commit both together.
  - **The opcode table** is the one generated family today. `include/opcodes.tsv` and
    `include/opcode-types.tsv` are the source. `tools/gen_opcodes.py` writes the C engine's tables,
    every port's, and the tree-sitter keyword lists; its `OUTPUTS` lists them. `make generate`
    regenerates them. The image build's preprocessing stage and the `generated_opcode_tables`
    meson test fail, with a diff, if a committed one is out of date.
  - **Adding a language port?** Its opcode table comes from the generator too: write an emitter
    and register it. The steps are in `tasks/reference/opcode-table-generator.md` ("Adding an
    engine port in a new language"), which also explains the data files and the decode column.
- C23; clang-format + clang-tidy; core `-Werror` (the image build is clang; gcc is checked by hand on a host build, and is clean too). `src/`+`include/` follow the
  shared standard `github.com/billsix/runClaudeInContainer:tasks/reference/c-coding-standard.md` (modernized through
  `tasks/archive/2026/11/10/c23-modernization-pass3.md` and the `const` pass
  `tasks/archive/2026/11/11/c23-const-correctness-pass.md`, which list the deliberate opt-outs; check new C
  for missing `const` by hand with `tools/const_candidates.py BUILDDIR`; no gate runs it); the core is additionally gated under
  `-Wconversion -Wsign-conversion` (clang in the image; gcc clean by hand), done in `tasks/archive/2026/10/16/integer-conversion-hardening.md`,
  and under `-Wswitch-enum`: every `switch` has a `default` and every `if`/`else if` chain an `else`
  (`tasks/archive/2026/11/11/c-switch-and-else-if-chains.md`; new C is checked with
  `python3 tools/c_dispatch_check.py BUILDDIR src`, which the meson test `dispatch_check` runs; it is the shared, vendored copy of
  `github.com/billsix/runClaudeInContainer:tasks/reference/c_dispatch_check.py`, kept byte-identical). `include/opcodes.tsv` is the single source of truth for
  the simulator, the ports and the tree-sitter grammar; edit it and run `make generate`, never the
  generated tables. **Any change to `opcodes.tsv` (or `opcode-types.tsv`) must be gated with
  `BUILD_TREE_SITTER=1`**: `grammar.js` builds its keyword rules from the generated
  `keyword-lists.js`, and a `BUILD_TREE_SITTER=0` image build never builds or tests the grammar.
- This is a learning tool: the explain/teaching output is a first-class feature,
  not debug spew. Keep it correct and legible.
- **Modernization scope — `src/` + `include/` only.** The shared C coding standard
  (`github.com/billsix/runClaudeInContainer:tasks/reference/c-coding-standard.md`) and modernization sweep
  (`github.com/billsix/runClaudeInContainer:tasks/reference/c-modernization-sweep.md`) apply to the simulator core.
  The **teaching C is deliberately NOT modernized**: `examples/` and `pgu/`'s C are
  paired C + MIPS-asm demos a student reads next to the generated assembly, so they
  are written for the clearest C→asm mapping — e.g. **all locals declared at the top
  of a function**, mirroring the stack-frame prologue — not for modern-C style. Opt
  out of any idiom (declare-at-use, heavy `const`, designated initializers,
  `typeof`/`auto`, helper extraction) that obscures the codegen a demo exists to show.
  Rationale + opt-out list: `tasks/reference/example-c-teaching-style.md`. The
  hand-written MIPS `.asm` demos follow a parallel **stack-visible** style (each C
  local in a `-N($fp)` slot, a per-routine columnar SYMBOL TABLE, and an ascii
  stack-frame SNAPSHOT with hand-traced values checked by
  `tools/check_asm_canonical.py`); deliberate register/idiom/bug demos are exempt.
  Guide: `tasks/reference/mips-asm-teaching-style.md` (the living reference). Every
  non-exempt demo follows it; a new demo must too (the conversion record is
  `tasks/archive/2026/10/31/asm-simplification-sweep*.md`).

## Continuous integration

`.github/workflows/checks.yml` runs on every push and pull request, as two jobs that are each
`checkout` then one `make` target. All logic lives in the Makefile, so the same command runs
locally. `CONTAINER_CMD` falls back to docker on the `ubuntu-latest` runner.
- **`format`:** `make check-format MINIMAL_IMAGE=1`, the formatters, then `git diff --exit-code`.
- **`test`:** `make image MINIMAL_IMAGE=1 RUN_SANITIZERS=1`. The image build is the test gate:
  the generated-table check, every meson suite, and the UBSan/ASan regression runs.

Neither job commits. A CI-facing target must work without a TTY (no `-it`; see
`tasks/archive/2026/11/13/remove-tty-flag-from-batch-make-targets.md`), must use absolute host
paths in `-v` (`$(CURDIR)`, because docker reads a bare `.` as a volume name), and must not pass
`podman build`-only flags to `build` (`ELPA_MOUNT` is empty when `USE_EMACS=0`). If CI needs a
step, add it as a `make` target first.

## Reviews

Latest deep review: `tasks/review/2026/11/11/repo-review.md` (the engine ports, the opcode
generator and the C dispatch work; carries over `tasks/review/2026/11/10/repo-review.md`, the
simulator core and full git history). Records are frozen; act on them through tasks.

## Tasks (in-flight)

All task docs (spim-internal *and* curriculum) live in `tasks/`; completed work
moves to `tasks/archive/<YYYY>/<MM>/<DD>/`. (The old separate `examples/tasks/`
surface has been folded into `tasks/`.) There is no separate handoff /
session-notes / next-session log — the top-level contents of `tasks/` are the
live picture, and git history plus the dated archive are the record of what's
done. Ordering / dependency guidance lives in `tasks/README.md` (§Ordering &
dependencies).
