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
  `Dockerfile`/`Makefile`/`docs/`). Port **complete**
  (`tasks/archive/2026/06/14/port-pgu.md`, 2026-05-25).
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

Two meson suites: **regression** (assembler, syscalls, exceptions,
teaching-mode goldens, AST parity; `tests/run-test.sh`) and **examples** (each demo
runs both the C binary and the spim asm, diffing stdout + exit status against
pinned goldens; `examples/tests/run-demo.sh`).

**Sanitizer gate** (`RUN_SANITIZERS=1`, the `make image` default; `make image
RUN_SANITIZERS=0` to skip): the image build also compiles **spim only** (the
`spimulator` target — the `-nostdlib` demos must not be sanitized) under
**UBSan-trap** (`-fsanitize=undefined -fsanitize-trap=undefined`) and **ASan**
(`-Db_sanitize=address`) and runs the regression suite under each, failing the
image on any UB or memory error. ASan leak detection is defaulted off in
`spim.c` via `__asan_default_options` (the gate is for corruption, not spim's
intentional exit-time leaks). Note: diagnostic UBSan (`-Db_sanitize=undefined`)
*under-reports* here — trap mode is the reliable gate. Rationale + the integer-UB
primer: `tasks/archive/2026/06/16/ubsan-sweep.md`.

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

Simulator / build (open):

- `container-build-cleanup.md` — the `exit()` bashrc trap drops the shell exit
  code (unquoted `$@` in the Dockerfile echo); bump the `fedora/43` dnf cache
  path to `44`. Both still present.
- `stdin-space-separated-ints.md` — syscall 5 reads only the first of
  space-separated piped ints; pick scanf-style semantics + regression test.
- `string-stream-to-memstream.md` — replace hand-rolled `str_stream` with POSIX
  `open_memstream` (~160 call sites).
- `parser-leak-cleanup.md` — parser drops transient identifiers/exprs;
  recommended fix: make PARSE_AST the only mode.
- `ast-column-tracking.md` — column begin/end on tokens and AST nodes.
- `program-listing-at-start.md` — `listing` REPL command + auto-listing under
  `-explain`.
- `software-alu.md` — (large) bit-level H&P-style ALU in `src/alu.c`,
  narratable by explain mode.
- `c23-modernization-pass2.md` — proposed second C23 pass (gnu23→c23 decision,
  constexpr / enum widths / attributes) across src/, examples/, pgu/.
- `codebase-cleanup-plan.md` — tiered cleanup: Tiers A + D done, F rejected;
  B (naming), C (header hygiene), E (test infra) remain.
- `fix-stale-doc-links.md` — repoint the archive README manifests' bare
  filenames to dated subdirs.

Curriculum (open):

- `container-cross-env.md` — bake clang + lld + qemu-user-static into the image
  so the multiarch shim is verifiable in-container (unblocks the two below).
- `examples-build-matrix.md` / `pgu-build-matrix.md` — cross-compile every C
  demo to `.s` listings for 5 ISAs at image build (`asm-out/<arch>/`).
- `libstr.md` — port ~10 musl string/memory functions as a teaching library.
- `symbol-tables.md` — per-demo `#SYMBOL TABLE` headers (C variable → MIPS
  location) + stack-frame diagrams; no demo has one yet.
- `unix-tools.md` — sbase/ubase ports; demos 09–24 landed; remaining: more
  stdin tools (strings, tail, od) and the CS-algorithms track.

Done but not yet archived (feature verified in the code; doc pending a move to
`tasks/archive/`): `explanation-levels-and-completion.md`,
`explanation-level-4-decoding.md`, `post-execute-narration.md`,
`header-clarity-and-box-drawing.md`, `repl-args-command.md`, and
`multiarch-shim.md` (5-arch `crt0.h` shipped; qemu verification lives on in
`container-cross-env.md`). `port-pgu.md` is archived
(`tasks/archive/2026/06/14/port-pgu.md`).
