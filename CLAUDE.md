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

Ordering / dependency guidance lives in `tasks/README.md` (§Ordering &
dependencies, reviewed 2026-07-07). Snapshot of the open set:

Quick wins (independent):

- `examples-install-location.md` — demo binaries were installed onto PATH;
  now go to `libexec/spimulator` (RPM convention). Implemented 2026-07-07;
  archive after `make image` confirms.
- `container-aslr-lldb.md` — container seccomp blocks lldb's `personality()`
  call; add `settings set target.disable-aslr false` to `.lldbinit`.
- `container-build-cleanup.md` — bashrc `exit()` trap drops the shell exit
  code; bump the `fedora/43` dnf cache path to `44`.
- `fix-stale-doc-links.md` — archive README manifests still list bare filenames.
- `stdin-space-separated-ints.md` — syscall 5 reads only the first of
  space-separated piped ints.
- `program-listing-at-start.md` — pre-execution disassembly dump (the REPL
  command must not be named `listing` — a `-listing` event-trace flag exists).
- `string-stream-to-memstream.md` — replace `str_stream` with POSIX
  `open_memstream` (~127 call sites).

Example-code hygiene (suggested order):

- `string-equality-audit.md` — find the "stops short" equality bug, which is
  **in the example code** per Bill (simulator sites audited clean 2026-07-07).
- `c-asm-comment-parity.md` — **remove** the embedded-C comment blocks from
  all example `.asm` files (decision 2026-07-07: delete, don't sync).
- `code-idiosyncrasies-audit.md` — sweep for oddities ("void argc" etc.):
  examples first, then pgu, then src/.

Curriculum / library:

- `libstr.md` — musl string/memory teaching library; **unblocked** — multi-file
  loading already works (`tests/tt.multifile.s`).
- `multi-file-load.md` — re-scoped 2026-07-07: the loading mechanism was
  already shipped; remaining scope is the shared musl library + de-duplicating
  the demos' private `atoi:`/`str_eq:`/`print_uint:` copies (libstr = tranche 1).
- `unix-tools.md` — nearly done (od/tail/tac/comm/cp/factor/seq/base64 and the
  CS-algorithms track all landed); remaining: `strings` + a hash demo.
- `examples-build-matrix.md` / `pgu-build-matrix.md` — 5-ISA `.s` listing
  matrices; unblocked (crt0.h landed, clang already in the image); coordinate
  MIPS endianness between them.
- `container-cross-env.md` — lld + qemu-user-static in the root Dockerfile;
  needed only for *runtime* cross verification, not for the matrices.

Simulator internals:

- `parser-leak-cleanup.md` → then `ast-column-tracking.md` — the leak fix
  deletes PARSE_DIRECT (PARSE_AST is already the default); do the column
  plumbing once, after.
- Naming pass: `opcode-types-descriptive-names.md` + `codebase-cleanup-plan.md`
  Tier B2/B3 as one sweep → then `c23-modernization-pass2.md` (its
  Tier-D-delivered items were struck 2026-07-07).
- `codebase-cleanup-plan.md` — remaining: Tier B2/B3 (naming), Tier C (header
  hygiene), Tier E3 (exception-path tests). Tiers A, D, B1, E1/E2/E4 done.
- Big swings, independent: `timing-model.md` (H&P ch.1 cycle model — cheaper,
  do first) and `software-alu.md` (bit-level ALU); share cycle vocabulary if
  both land.

Archived 2026-07-07 after verifying the features shipped:
`explanation-levels-and-completion`, `explanation-level-4-decoding`,
`post-execute-narration`, `header-clarity-and-box-drawing`,
`repl-args-command`, `multiarch-shim`, `symbol-tables` →
`tasks/archive/2026/07/07/`.
