# spimulator

A fork of **SPIM** (James Larus's MIPS R2000/R3000 assembly simulator) maintained
by William Emerison Six as a teaching tool. The core is a text-based MIPS32
interpreter; the fork adds a modern build, a worked-examples curriculum, a MIPS
port of the *Programming from the Ground Up* book, an editor grammar, and a
"teaching mode" that explains instructions as they execute.

## Status

- **Build:** Meson + Ninja (replaced the legacy Make/xmkmf). GNU C23
  (`c_std=gnu23`). One option: `-Dline_editing` (libedit REPL history; default auto).
- **Parser:** hand-written recursive-descent scanner/parser (`scanner.c` /
  `parser.c`) — flex+bison were removed (Phase 5, 2026-05).
- **Teaching mode:** `explain.c` renders instructions at levels 0–4 (mnemonic →
  disassembly → register before/after → bit-layout diagram → field decoding).
- The image builds + runs the full test suite at build time and fails on any
  regression.

## Layout

- `src/` — simulator: `spim.c` (REPL), `run.c` (execute), `memory.c`,
  `instruction.c` (codec / disassemble / explain), `syscall.c`, `scanner.c`,
  `parser.c`, `ast.c`, `pseudo-op.c`, `symbol-table.c`, `explain.c`;
  `exceptions.s` (the trap handler). `include/` — `opcodes.h` (X-macro opcode
  table), `registers.h`, `spim.h`, parser/AST headers.
- `examples/` — paired **C + MIPS-asm** teaching demos (`intro/`, `algorithms/`,
  `transforms/`, `fileio/`, `arguments/`, `recursion/`, `extras/`, plus
  `lib/libctype` + `libstdlib` adapted from musl). `examples/src/meson.build`
  builds them; `examples/src/Makefile` materializes native `.s` listings +
  binaries; `examples/tests/run-demo.sh` runs C and asm and diffs both against
  goldens. Curriculum design is in `examples/READING-ORDER.md` and the curriculum task docs in `tasks/`.
- `pgu/` — the *Programming from the Ground Up* book ported to MIPS/spim (its own
  `Dockerfile`/`Makefile`/`docs/`). Port **complete** (`tasks/port-pgu.md`, 2026-05-25).
- `tree-sitter/` — editor grammar; `grammar.js` keyword lists are derived from
  `opcodes.h` via `scripts/extract-keywords.py`, so they stay in sync.
- `tests/` — regression suite (`run-test.sh`, `tt.*.s` / `tt.*.in`).
- `Documentation/spim.1`, `meson.build`, `meson_options.txt`, `.clang-format`,
  `.clang-tidy`.

## Build / container workflow

Standalone: `meson setup builddir --buildtype=debug -Dline_editing=enabled &&
meson compile -C builddir && meson test -C builddir`.

Container (Fedora-44 + podman family template) — build args `USE_EMACS`,
`BUILD_TREE_SITTER`, `BUILD_DOCS`:

- `make image` — build + test spim at image-build time; build the examples' native
  artifacts; optionally build the tree-sitter grammar / Emacs integration.
- `make shell` *(default; runs `format` first)* — dev shell.
- `make format` — clang-format; `lint.sh` runs `clang-tidy`. Both run on shell exit.
- `make html` / `pdf` / `epub` / `docs` *(BUILD_DOCS)* — build the **pgu** book via
  sphinx-build directly (the Makefile rasterizes SVGs and calls `sphinx-build`
  rather than `pgu/docs/Makefile`, which routes through an interactive aspell step).

## Tests

Two meson suites: **regression** (21 cases — assembler, syscalls, exceptions,
teaching-mode goldens, AST parity; `tests/run-test.sh`) and **examples** (each demo
runs both the C binary and the spim asm, diffing stdout + exit status against
pinned goldens; `examples/tests/run-demo.sh`).

## Conventions

- C23; clang-format + clang-tidy. `opcodes.h` is the single source of truth for
  both the simulator and the tree-sitter grammar — regenerate the grammar rather
  than hand-editing keyword lists.
- This is a learning tool: the explain/teaching output is a first-class feature,
  not debug spew. Keep it correct and legible.

## Tasks (in-flight)

All task docs (spim-internal *and* curriculum) live in `tasks/`; completed work
moves to `tasks/archive/<YYYY>/<MM>/<DD>/`. (The old separate `examples/tasks/`
surface has been folded into `tasks/`.) There is no separate handoff /
session-notes / next-session log — the current `tasks/` contents are the live
picture, and git history plus the dated archive are the record of what's done.

- [`tasks/container-build-cleanup.md`](tasks/container-build-cleanup.md) — the
  `exit()` bashrc trap drops the shell exit code; bump the `fedora/43` dnf cache
  path to `44`.
- [`tasks/fix-stale-doc-links.md`](tasks/fix-stale-doc-links.md) — repoint
  intra-repo markdown links broken by the archive/plan→task reorg (doc hygiene).
- Curriculum tasks (now in `tasks/`): `examples-build-matrix.md`,
  `container-cross-env.md`, `libstr.md`, `multiarch-shim.md`, `symbol-tables.md`,
  `unix-tools.md`.
- Reference / open: teaching-mode polish (`explanation-levels-*`,
  `post-execute-narration`), parser cleanup (`parser-leak-cleanup`,
  `ast-column-tracking`); `port-pgu.md` is done.
