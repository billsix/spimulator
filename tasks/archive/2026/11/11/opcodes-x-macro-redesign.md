# One opcode data file, one generator, and generated C, Python, Haskell and Rust tables

**Created:** 2026-10-16 (William Emerison Six <billsix@gmail.com>); reframed 2026-11-11 after the engine ports
**Completed:** 2026-11-11
**Branch:** `opcodes-generator`

## Outcome

The opcode table's source is now `include/opcodes.tsv`, with the operand-shape tags in
`include/opcode-types.tsv`. `tools/gen_opcodes.py` generates from them every table in the repo:
- the C engine's `tokens.h`, `opcode-types.h`, `opcode-tables.h` and `src/opcode-tables.c`;
- the Python, Haskell and Rust ports' tables;
- the tree-sitter grammar's keyword lists.

The generated files are committed. The image build regenerates in a preprocessing stage and fails
with a diff if a committed file is stale, and so does the meson test `generated_opcode_tables`.
Developers run `make generate`.

The X-macro (`include/opcodes.h`) and the enum-body `#include` in `tokens.h` are retired, and so are
the C engine's startup `qsort` of its three tables and the four per-consumer parsers and generators.
The design, the decode column, and how a new language plugs in are in
`tasks/reference/opcode-table-generator.md` (renamed and rewritten from `opcodes-x-macro.md`).

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

- **Generate the C tables too,** not only the ports'. This was the maintainer's design. It replaced
  an earlier recommendation, made the same day, to keep C's X-macro and generate only the ports'
  tables. Reasons:
  - the shared-encoding answer is settled once, for every language;
  - the source is a plain data file, not C macro syntax that each other consumer parsed with a
    regular expression;
  - the generated C is plain tables a student can read.
- **The image build is the gate:** a preprocessing stage in the Dockerfile, plus the meson test.
  `README.md` says what developers do, and `CLAUDE.md` says development goes through the
  Dockerfile.
- **The staleness check regenerates and compares byte for byte.**
  - Rejected: a checksum manifest. Regeneration from an unchanged data file reproduces the stored
    hashes, so a hand-edited committed file would pass.
  - Rejected: git hooks. The image build is the gate.
- **The source is a new data file from the first step (option (b)).** The generator first wrote
  `opcodes.h` and `opcode-types.h` byte-identical to the hand-written ones, as the first parity
  check. The other option, (a), kept `opcodes.h` as the source until the C cut-over; it would have
  needed the parser written twice.
- **New languages plug into the generator.** At the maintainer's request (more languages may come),
  the steps are written up in the reference doc, "Adding an engine port in a new language", and
  linked from `CLAUDE.md`, `port/README.md` and `tasks/reference/engine-ports-design.md`.

## Steps, as committed

1. **The data files and the generator.** `tasks/adhoc/opcodes-x-macro-redesign/to_tsv.py`
   (one-shot) converted the two headers, keeping order, groups and comments. The decode column's
   seven marks came from the Rust port's table, which `gen_opcodes.c` had computed by running the
   C engine's own `qsort` and `bsearch`.
   - The regenerated `opcode-types.h` was byte-identical.
   - `opcodes.h` differed in one comment: `/* MIPS32 Rev 2*/` gained the space every other row has.
2. **The checks and the developer path:**
   - the Dockerfile stage, which needed `.clang-format` copied before it rather than at the end;
   - the meson test and `make generate`;
   - `README.md` "Changing the opcode table";
   - `CLAUDE.md`.

   The README and `CLAUDE.md` also gave the image's Fedora version as 44; it is 45.
3. **The ports.**
   - The Haskell `Opcodes.hs` and Rust `opcodes_table.rs` regenerated with identical data; only
     comments changed.
   - The hand-written `OpcodeTypes.hs` and `opcode_types.rs` became generated, with the same
     constructors in the same order.
   - The Python port's `opcode_table.py` holds the `Tok` and `OpType` `IntEnum`s, the rows and the
     decode table. `opcodes.py` stopped parsing `opcodes.h` at import.
   - The Haskell and Python ports read the unique-key decode table instead of emulating glibc's
     sort.
   - `GenOpcodes.hs`, `gen_opcodes.c` and `gen_opcodes.sh` were removed.
4. **Tree-sitter** (done before the C cut-over, because its scripts read `opcodes.h`). The generator
   writes `keyword-lists.js`, identical apart from its header.
   - `extract-keywords.py`, `build-keyword-lists.py` and `keywords.json` were removed.
   - The tree-sitter `Makefile` only builds and tests the parser. Its `make clean` had been deleting
     the committed lists.
   - `grammar.js`'s header said the lists were pasted inline; it requires the file.
5. **The C cut-over.**
   - The generated `tokens.h` and three `const` tables replace the five X-macro expansions. One
     table, `opcode_tbl`, serves the scanner, the parser and the disassembler, because the file
     order is both name order and token order.
   - `initialize_inst_tables` is gone, along with the three sorts. Two of them were no-ops; only
     the decode order depended on `qsort`.
   - `src/dump-opcodes.c`, never built and not compiling (review F10), was removed.
6. **Docs:**
   - the reference doc;
   - `CLAUDE.md` (layout, conventions, the `BUILD_TREE_SITTER=1` rule now naming `opcodes.tsv`);
   - `port/README.md` and the ports design doc;
   - stale `opcodes.h` mentions in reference and open-task docs. `tasks/codebase-cleanup-plan.md`
     G2 ("don't replace with a Python script") is marked superseded.

## Verification

- `tasks/adhoc/opcodes-x-macro-redesign/check_decode.c` redid the old decoder (`qsort` and glibc
  `bsearch` over every row) and compared all 286 encodings with the generated decode table: 0
  mismatches.
- At each step:
  - the generator's `--check`;
  - clang and gcc at `-Werror`, and clang-tidy clean on the changed C files;
  - the port suites: Python 71 OK; Haskell and Rust 142 OK, each with the whole-tree comparison
    with C;
  - the generated Python module is clean under ruff, ty and `check_local_annotations.py`;
  - the Haskell port builds with `-Wall -Werror`.
- A hand-edited generated file makes `--check` fail; restored, it passes.
- After the cut-over, the full `meson test` gave 284 OK.
- The grammar's `make` (tree-sitter generate and test) passes 5 of 5.
- `make image` with the defaults (`BUILD_TREE_SITTER=1`, sanitizers on) passed on the final commit:
  the generator check, the build, every meson suite, and the UBSan and ASan regression runs.

## Follow-ups

- `tasks/python-port-match-statements.md` step 2 is now only the conversion of the ~290 lookups:
  the `Tok` and `OpType` `IntEnum`s exist.
