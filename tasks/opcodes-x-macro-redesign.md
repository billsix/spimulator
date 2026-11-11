# One opcode data file, one generator, and generated C, Python, Haskell and Rust tables

**Status:** proposed — needs go-ahead (design settled; implementation not started)
**Priority:** 5
**Difficulty:** 7
**Created:** 2026-10-16 (William Emerison Six <billsix@gmail.com>); reframed 2026-11-11 after the engine ports

## BLUF

Replace the `include/opcodes.h` X-macro with one data file listing every instruction,
pseudo-instruction and directive, and one Python tool, `tools/gen_opcodes.py`, that generates
from it:
- the C engine's tables;
- the Python, Haskell and Rust ports' tables;
- the tree-sitter grammar's keyword lists.

The generated files are committed. The Dockerfile build runs the generator as a preprocessing
stage and fails if its output differs from what is committed. Developers regenerate with one `make`
target and commit the result. The generator also writes each decode table already sorted, with one
row per encoding, which removes the engines' dependence on the order glibc's `qsort` leaves equal
keys in.

Done means:
- one command regenerates every table;
- a stale committed file fails the image build and `meson test`;
- every table is identical to today's, and every suite passes;
- `README.md` and `CLAUDE.md` tell developers what to do.

## Context

- **How the X-macro works:** the 381 rows, four columns, seven C consumers and the enum-body
  `#include` in `tokens.h` are in `tasks/reference/opcodes-x-macro.md`.
- **Who reads `opcodes.h` today** (checked 2026-11-11):
  - **The C engine:** `include/tokens.h`, `src/scanner.c`, `src/parser.c`, `src/instruction.c` and
    `src/dump-opcodes.c` each define `OP` and include the file.
  - **The tree-sitter grammar:** `tree-sitter/scripts/extract-keywords.py` parses it as text into
    `keywords.json`, and `build-keyword-lists.py` turns that into `keyword-lists.js`. Both outputs
    are committed. The `BUILD_TREE_SITTER=1` rule in `CLAUDE.md` exists for this pipeline.
  - **The Haskell port:** `port/haskell/tools/GenOpcodes.hs` writes `port/haskell/src/Opcodes.hs`,
    which is committed.
  - **The Rust port:** `port/rust/tools/gen_opcodes.sh` compiles `port/rust/tools/gen_opcodes.c`
    (which includes `opcodes.h` as the C engine does) and writes `port/rust/src/opcodes_table.rs`,
    which is committed.
  - **The Python port:** `port/python/spimulator_py/opcodes.py` parses the rows with a regular
    expression at import. `tasks/python-port-match-statements.md` decided to replace this with a
    committed, generated module of `IntEnum` classes.
  - **Hand-written copies of `opcode-types.h`,** the operand-shape tags, that no generator writes:
    `port/haskell/src/OpcodeTypes.hs` and `port/rust/src/opcode_types.rs`.
- **Nothing checks the generated files.**
  - On 2026-11-11 both port tables were regenerated and matched byte for byte, but only because
    `opcodes.h` has not changed since 2026-07-29 (`3fa59b2`).
  - An edit that forgets one generator leaves that port decoding with an old table, unreported.
- **Rows that share an encoding.** Six pairs of real instructions have the same value in the
  encoding column: `floor.w.s`/`prefx`, `ldxc1`/`madd.d`, `lwxc1`/`madd.s`, `round.l.s`/`swxc1`,
  `sdxc1`/`trunc.l.s`, `suxc1`/`trunc.w.s`.
  - The C engine sorts a copy of the table with `qsort` at startup and decodes with `bsearch`
    (`instruction.c`). Which row of a pair it finds depends on glibc's sort order and on where
    `bsearch`'s halving lands.
  - The Python and Haskell ports copy both, assuming glibc's `qsort` is a stable merge sort
    (`port/python/spimulator_py/opcodes.py`, `port/haskell/src/OpcodeTables.hs`). The Rust generator
    gets the answer by running the C code.
  - All three match the C engine on Fedora 45's glibc. But the answer is a libc implementation
    detail, for the C engine as much as for the ports.
- **Committed generated sources** are a rule in `CLAUDE.md` (Conventions, added 2026-11-11). The
  reason is the one upstream had for committing its flex/bison output until `ebe63b4`: the build
  needs no generator toolchain, and the diff shows what a change to the source did.
- **The maintainer's design** (William Emerison Six <billsix@gmail.com>, 2026-11-11):
  - one tool processes one source file and generates C, Rust, Haskell and Python files;
  - the generation runs as a preprocessing stage of the Dockerfile build;
  - `README.md` states what developers must do;
  - development goes through the Dockerfile by default, and `CLAUDE.md` says so.

  This replaces the earlier recommendation, made the same day, to keep C's X-macro and generate
  only the ports' tables. The reasons for generating C too:
  - the shared-encoding answer is then settled once, in the generator, for all four languages;
  - the source no longer has to be C macro syntax that every other consumer parses with a regular
    expression;
  - the generated C (plain enums and tables) is easier for a student to read than an X-macro, and
    it retires the enum-body `#include`.
- **The earlier framing (2026-10-16)** was "keep the X-macro or replace it", as an investigation,
  defaulting to keep. The ports changed the balance: four languages now need the table.

## How the generator fits the build and development

- **Committed, and checked in the image build.** The Dockerfile's preprocessing stage runs
  `tools/gen_opcodes.py` over the copied source and compares its output with the committed files.
  Any difference fails the build, with a message naming the stale files and the command that fixes
  them. The build regenerates, but never builds from anything other than what is committed, so a
  stale commit is reported, not hidden. (Decisions.)
- **The same check as a `meson test`,** so a local build catches it too.
- **The developer step is one `make` target** (for example `make generate`). It runs the tool in the
  container and writes the generated files into the bind-mounted tree; the developer commits them
  with the change to the data file. `README.md` gets a short "When you change the opcode table"
  section with the commands, labelled `[HOST]`/`[CONTAINER]` as the README does elsewhere.
- **Effect on the ports as the C engine changes:**
  - A change to the opcode table (a new instruction, a renamed mnemonic) is one edit to the data
    file plus `make generate`. All four languages' tables update together.
  - A new instruction then fails the port suites until each port implements its execution. That is
    the signal for what to carry into each port.
  - Other C changes reach the ports as they do now: the C engine's suites run against each port
    (`port/meson.build`) and fail until the port follows (`tasks/reference/engine-ports-design.md`).

## Decisions

- **The staleness check regenerates and compares byte for byte** (William Emerison Six
  <billsix@gmail.com>, 2026-11-11). The Dockerfile stage and the `meson test` run the generator
  from the data file and compare its output with the committed files; any difference fails, and
  the message names the files and shows what differs.
  - **Rejected: a checksum manifest** (several hashes per generated file, checked on regeneration).
    Regeneration from an unchanged data file reproduces the stored hashes, so a hand-edited
    committed file would pass unexamined. A mismatch would also say less than a diff, the manifest
    is one more file to keep in sync, and several hashes guard against deliberate collisions, which
    git diffs already expose.
  - **Rejected: git hooks or a commit-time CI check.** The image build is the gate, as for the rest
    of the project. A bad commit is reported at the next build, not prevented.

- **The source is a new data file from step 1** (William Emerison Six <billsix@gmail.com>,
  2026-11-11, option (b) of two). The file is a TSV, or TOML read with Python's `tomllib`; the format
  is chosen in step 1. Until the C cut-over (step 4), the generator also writes
  `include/opcodes.h` and `include/opcode-types.h`, byte-identical to today's, so the C engine's
  X-macro consumers keep working unchanged. Under this option:
  - `opcodes.h` is a generated, committed file from the start;
  - there is never more than one hand-edited source;
  - the generator's parser is written once, for the final format;
  - the byte-identical `opcodes.h` is the first parity check.

  The other option, (a), kept `opcodes.h` as the source until the cut-over. It was not chosen because
  the parser would be written twice, and the move would happen in the riskiest step.

## Plan

Each step is a commit or several, gated by `make image` with `BUILD_TREE_SITTER=1` (the image build
runs the full `meson test`, port suites included) and by the generator's own parity check.

1. **The data file and the generator** (Decisions: the source). Convert today's 381 rows to the
   data file. The generator's first output is `include/opcodes.h` and `include/opcode-types.h`,
   byte-identical to today's; then add the per-language outputs one at a time. Settle
   the shared-encoding rule here. Write each decode table with one row per encoding, choosing today's
   answer for the six pairs (computed once from the current C engine, and listed in the data file
   or the generator with a comment). Rules: the `codegen-conventions.md` reference doc of
   github.com/billsix/runClaudeInContainer (structured output, a parity harness, run-to-run
   determinism, a "generated, do not edit" header).
2. **The checks and the developer path:**
   - the Dockerfile preprocessing stage and the `meson test`;
   - the `make generate` target;
   - the `README.md` developer section;
   - in `CLAUDE.md`: the new source file; that `opcodes.h` and `opcode-types.h` are now generated
     (the `opcodes.h` rule in Conventions and the `BUILD_TREE_SITTER=1` rule now name the data
     file); the generated-files list; and that development goes
     through the Dockerfile: the image build is the gate, and the generator's check is part of it.
3. **The ports:**
   - generate the Python port's `IntEnum` module (`tasks/python-port-match-statements.md` step 2
     becomes this output);
   - generate Haskell's `Opcodes.hs` and `OpcodeTypes.hs`, and Rust's `opcodes_table.rs` and
     `opcode_types.rs`;
   - delete `GenOpcodes.hs`, `gen_opcodes.c`/`.sh`, and the two hand-written tag files;
   - the ports read the unique-key decode table instead of emulating `qsort` and `bsearch`;
   - verify with the port suites and `port/harness/compare_all_programs.py` for each port.
4. **The C cut-over.**
   - The generator writes plain C: the `TOK_*` enum, the keyword table, the op-type table and the
     instruction tables, already sorted. `initialize_inst_tables` stops calling `qsort`.
   - The X-macro consumers, the enum-body `#include` in `tokens.h`, and the generated `opcodes.h`
     and `opcode-types.h` are retired.
   - Prove identical behavior: `dump-opcodes` output, the AST-parity and disassembly goldens, and
     the full `meson test`, the same before and after.
   - Update `tasks/reference/opcodes-x-macro.md` to describe the generator, or replace it with a
     reference doc for it.
5. **Tree-sitter:** `keywords.json` and `keyword-lists.js` come from the generator, and
   `extract-keywords.py` is retired.

## Constraints

- **`_Generic` is ruled out** (2026-10-16). It dispatches on one expression's type and cannot iterate
  a table. Reasoning: `tasks/reference/opcodes-x-macro.md`.
- **Every table must stay identical at every step,** in every language. The AST-parity goldens, the
  disassembly tests and the port suites are the oracle.
- **The build never needs to regenerate to succeed.** The committed files are complete, and the
  Dockerfile stage only confirms they are current.

## Cross-links

- `tasks/reference/opcodes-x-macro.md`: how the X-macro works, and the design analysis.
- `tasks/python-port-match-statements.md`: its step 2 becomes this generator's Python output.
- `tasks/reference/engine-ports-design.md`: the rules every port follows.
- `tasks/archive/2026/10/16/c23-modernization-pass2.md`: the modernization this task was split out of.
