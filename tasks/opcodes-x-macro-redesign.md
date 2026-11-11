# One generator for every port's opcode tables; the C engine keeps its X-macro

**Status:** proposed — needs go-ahead (one decision, Open questions; the maintainer wants to discuss it first)
**Priority:** 5
**Difficulty:** 5
**Created:** 2026-10-16 (William Emerison Six <billsix@gmail.com>); reframed 2026-11-11 after the engine ports

## BLUF

`include/opcodes.h` is the single list of every instruction, pseudo-instruction and directive. The
C engine reads it through an X-macro. Since the engine ports, three more programs depend on it, each
handled differently, and nothing checks that their copies are current. The recommendation:
- Keep the C engine's X-macro.
- Write one Python tool, `tools/gen_opcodes.py`, that generates every port's tables from
  `opcodes.h` and `opcode-types.h`.
- Commit the generated files.
- Fail the Dockerfile build and `meson test` when a committed file is out of date.
- Make the C engine's choice between rows that share an encoding explicit, so any generator can
  reproduce it.

Done means one command regenerates every port's tables, a stale file fails the gates, and the port
suites pass.

## Context

- **How the C side works:** the 381-row table, its four columns, the seven C consumers, the
  enum-body `#include` in `tokens.h` and the design analysis are in
  `tasks/reference/opcodes-x-macro.md`.
- **Who reads `opcodes.h` today** (checked 2026-11-11):
  - The C engine: `tokens.h`, `scanner.c`, `parser.c`, `instruction.c` and `dump-opcodes.c` define
    `OP` and include the file.
  - The tree-sitter grammar: `tree-sitter/scripts/extract-keywords.py` parses it as text into
    `keywords.json`, and `build-keyword-lists.py` turns that into `keyword-lists.js`. Both outputs
    are committed. The `BUILD_TREE_SITTER=1` rule in `CLAUDE.md` covers them.
  - The Haskell port: `port/haskell/tools/GenOpcodes.hs` (a Haskell program) writes
    `port/haskell/src/Opcodes.hs`, which is committed.
  - The Rust port: `port/rust/tools/gen_opcodes.sh` compiles `port/rust/tools/gen_opcodes.c`, which
    includes `opcodes.h` as the C engine does, and writes `port/rust/src/opcodes_table.rs`, which is
    committed.
  - The Python port: `port/python/spimulator_py/opcodes.py` parses `opcodes.h` with a regular
    expression at import. `tasks/python-port-match-statements.md` has decided to replace that with a
    committed, generated module of `IntEnum` classes.
- **Hand-written copies of `opcode-types.h`,** the operand-shape tags, which no generator writes:
  `port/haskell/src/OpcodeTypes.hs` and `port/rust/src/opcode_types.rs`.
- **Nothing checks the generated files.**
  - On 2026-11-11 both were regenerated and matched byte for byte. They match only because
    `opcodes.h` has not changed since 2026-07-29 (`3fa59b2`).
  - A future edit to `opcodes.h` that forgets one generator leaves that port decoding with an old
    table, and nothing would say so until a test happened to touch the changed row.
- **Rows that share an encoding.** Six pairs of real instructions have the same value in the
  encoding column: `floor.w.s`/`prefx`, `ldxc1`/`madd.d`, `lwxc1`/`madd.s`, `round.l.s`/`swxc1`,
  `sdxc1`/`trunc.l.s`, `suxc1`/`trunc.w.s`.
  - The C engine decodes a word by sorting a copy of the table with `qsort` and searching it with
    `bsearch` (`instruction.c`). Which row of a pair it finds depends on the order glibc's `qsort`
    leaves equal keys in.
  - The Python and Haskell ports copy that order on the assumption that glibc's `qsort` is a stable
    merge sort (`port/python/spimulator_py/opcodes.py`, `port/haskell/src/OpcodeTables.hs`). The
    Rust generator gets the answer by running the C sort.
  - All three match the C engine on Fedora 45's glibc, but the answer is a libc implementation
    detail, for the C engine as much as for the ports.
- **Generated sources are committed,** a rule now in `CLAUDE.md` under Conventions. It was added
  2026-11-11 at the maintainer's request, for the same reason upstream committed its flex/bison
  output until `ebe63b4`: the build needs no generator toolchain, and the diff shows what a change
  to the source did.
- **The earlier framing of this task (2026-10-16):** keep the X-macro or replace it with external
  code generation, as an investigation. That question is answered by keeping it: the C build reads
  the table directly, and its tables cannot drift from it. What is new is the ports.

## The recommendation

1. **The C engine keeps the X-macro.** It works, its tables cannot drift from `opcodes.h`, and
   replacing it would add a generator step to the C build for no gain. The enum-body `#include` in
   `tokens.h` stays, a documented cost.
2. **One Python tool, `tools/gen_opcodes.py`,** reads `include/opcodes.h` and
   `include/opcode-types.h` and writes:
   - the Python port's `IntEnum` module, as `tasks/python-port-match-statements.md` decided;
   - `port/haskell/src/Opcodes.hs` and `port/haskell/src/OpcodeTypes.hs`;
   - `port/rust/src/opcodes_table.rs` and `port/rust/src/opcode_types.rs`.

   It replaces `GenOpcodes.hs`, `gen_opcodes.c`/`.sh`, and the two hand-written tag files. Python
   suits it:
   - the rows are plain literals (rows wrapped over two lines are the only irregularity);
   - the Python port and `extract-keywords.py` already parse the same rows;
   - one language for every target is easier to maintain than one generator per port.

   The tree-sitter scripts can stay as they are, or read the tool's parse later.
3. **Committed, and checked rather than regenerated in the build.** `tools/gen_opcodes.py --check`
   writes into a scratch directory and fails if any committed file differs. It runs:
   - in the Dockerfile build stage, so the image is built from exactly what is committed;
   - as a `meson test`, so a local build catches a stale file too.

   Regenerating inside the image would hide a stale commit instead of reporting it.
4. **Make the shared-encoding choice explicit in C first.** Give `instruction.c`'s sort a tie-break
   (for example on the token) that reproduces today's six answers. Prove it with `dump-opcodes` and
   the AST-parity and disassembly goldens before and after. Then the generator can compute the answer
   instead of copying glibc, and the C engine no longer depends on `qsort`'s order.
5. **The generated files follow the codegen rules:** a parity check against today's committed
   files, run-to-run determinism, and a "generated, do not edit" header. Rules: the
   `codegen-conventions.md` reference doc in github.com/billsix/runClaudeInContainer.

## How this affects the ports as the C engine changes

- **A change to `opcodes.h`** (a new instruction, a renamed mnemonic) means running
  `tools/gen_opcodes.py` and committing its output with the change. If that is forgotten, the
  Dockerfile build and `meson test` fail.
- **A new instruction** then fails the port suites until each port implements its execution. That
  is the signal for what to carry into each port.
- **Other C changes** reach the ports as they do now: the C engine's own suites run against each
  port (`port/meson.build`) and fail until the port follows (`tasks/reference/engine-ports-design.md`).

## Open questions

1. Adopt the recommendation above (C keeps its X-macro; one Python generator writes every port's
   tables; committed; `--check` in the Dockerfile and meson; an explicit tie-break in C first)?
   - The alternative is to keep the three per-port mechanisms and add only a staleness check for
     each. That is less work, but it keeps three generators in three languages and the two
     hand-written tag files.

   Recommendation: adopt it. The maintainer wants to discuss this before deciding (2026-11-11).

## Constraints

- **`_Generic` is ruled out** (2026-10-16). It dispatches on one expression's type and cannot iterate
  a table. The reasoning is in `tasks/reference/opcodes-x-macro.md`.
- **Any change touching `opcodes.h` or `opcode-types.h` is verified with `make image
  BUILD_TREE_SITTER=1`** (`CLAUDE.md`, Conventions).
- **Every C table must stay identical:** the AST-parity goldens and the full `meson test`, port
  suites included, are the oracle.

## Cross-links

- `tasks/reference/opcodes-x-macro.md`: how the X-macro works, and the design analysis.
- `tasks/python-port-match-statements.md`: its step 2 (a generator for the Python port's `IntEnum`
  module) becomes this tool's Python output if this is adopted.
- `tasks/reference/engine-ports-design.md`: the rules every port follows.
- `tasks/archive/2026/10/16/c23-modernization-pass2.md`: the modernization this task was split out of.
