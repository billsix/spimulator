# The opcode table generator: one data file, every language's tables

**Reference document.** Read it before changing `include/opcodes.tsv`, `include/opcode-types.tsv`
or `tools/gen_opcodes.py`, or before adding an engine port in a new language. It describes the
design as of 2026-11-11 (`tasks/archive/2026/11/11/opcodes-x-macro-redesign.md`).

## In one paragraph

`include/opcodes.tsv` lists every MIPS instruction, assembler pseudo-instruction, assembler
directive and REPL command spimulator knows, one row each. `include/opcode-types.tsv` lists the
operand-shape tags the rows carry. `tools/gen_opcodes.py` reads both and writes every table built on
them:
- the C engine's token enum and lookup tables;
- each engine port's table (Python, Haskell, Rust);
- the tree-sitter grammar's keyword lists.

The generated files are committed. Upstream SPIM committed its flex/bison output for the same
reason: building needs no generator, and a diff shows what a change did. `make image` regenerates
in a preprocessing stage and fails, with a diff, if anything committed differs. Developers run
`make generate` and commit the data file together with what it rewrote.

## The data files

- **`include/opcodes.tsv`**, tab-separated. `#` lines are comments, and an empty line starts a new
  group (kept as a blank line in the C output). The columns:
  - `name`: the mnemonic as written (`add`, `.word`, `li`).
  - `token`: the C token name (`TOK_ADD_OPCODE`). Tokens are numbered `264 + row index`, after
    eight structural tokens from 256 (`TOK_EOF` … `TOK_FP`). `-print-ast` prints these numbers, so
    every engine uses the same ones.
  - `type`: an operand-shape tag from `opcode-types.tsv`.
  - `encoding`: the instruction's fixed bits, as the C source writes them (`0x00000020`), or `-1`
    for a directive or pseudo-instruction.
  - `decode`: `*` on the one row a binary word decodes to, when several rows share its encoding;
    empty otherwise.
  - `comment`: an optional note, kept as a C comment.
- **`include/opcode-types.tsv`**: `name`, `value` (0..42, a `uint8_t`) and `comment` (an example of
  the operand list, padded in the C output so the closing `*/` lines up).
- **Rules the generator enforces** (it refuses to write anything otherwise):
  - names in strictly increasing byte order, because the C scanner finds keywords with `bsearch`;
  - no repeated token or tag;
  - every row's tag is defined;
  - exactly one `*` per shared encoding, and none elsewhere.

## The decode column

Seven encodings are shared: `-1` (every directive and pseudo-instruction) and six pairs of real
instructions with the same fixed bits, such as `floor.w.s`/`prefx` and `lwxc1`/`madd.s`.

The old C engine sorted all 381 rows by encoding with `qsort` at startup and decoded with `bsearch`.
Which row of a pair it found depended on glibc's sort and on where the search's halving landed.

Now each decode table has one row per encoding, already sorted. The `*` marks preserve exactly the
old answers. A one-shot program, `check_decode.c` (in git under `tasks/adhoc/opcodes-x-macro-redesign/`
before the archive commit of `tasks/archive/2026/11/11/opcodes-x-macro-redesign.md`), reran the old
algorithm against every encoding and found no difference. To change which row an encoding decodes to, move the `*`.

## What it writes (`OUTPUTS` in `tools/gen_opcodes.py`)

| File | For | Holds |
|---|---|---|
| `include/tokens.h` | C | the `TOK_*` enum |
| `include/opcode-types.h` | C | the `op_type` enum |
| `include/opcode-tables.h`, `src/opcode-tables.c` | C | `opcode_tbl` {name, token, op_type}, sorted by name and by token; `opcode_encoding_tbl` {name, token, encoding}; `opcode_decode_tbl` {name, encoding, token}, one row per encoding. `OPCODE_COUNT` and `OPCODE_DECODE_COUNT` are `constexpr`. |
| `port/python/spimulator_py/opcode_table.py` | Python | the `OpType` and `Tok` `IntEnum`s, `ROWS`, `DECODE` |
| `port/haskell/src/Opcodes.hs`, `OpcodeTypes.hs` | Haskell | the `Opcode` and `OpType` types, `opcodeRows`, `opcodeDecodeRows` |
| `port/rust/src/opcodes_table.rs`, `opcode_types.rs` | Rust | `OPCODE_ROWS`, the `TOK_*` constants, `DECODE_TABLE`, the `OpType` enum |
| `tree-sitter/scripts/keyword-lists.js` | the grammar | the mnemonics of each operand-shape category |

How each output is produced:
- C output goes through `clang-format` with the repo's `.clang-format`, run from the repo root.
- Every other output is written in its final form, with no formatter. The Python module is what
  `ruff format` would write.
- Every output starts with a "GENERATED … do not edit" header.
- The output is deterministic: the same data gives the same bytes.

## The check, and the developer path

- **The image build.** The Dockerfile's preprocessing stage, right after the source is copied, runs
  `python3 tools/gen_opcodes.py --check`. It writes nothing; it fails with a unified diff naming
  each stale file. `.clang-format` and `tree-sitter/scripts/keyword-lists.js` are copied before this
  stage for that reason.
- **The meson test** `generated_opcode_tables`, in the regression suite, runs the same check.
- **The developer command.** `make generate` runs the tool on the existing image and rewrites the
  files in the bind-mounted tree. It deliberately doesn't depend on `make image`: after a `.tsv` edit
  the image build's check fails, so rebuilding first would stop the generator from ever running. The
  host fallback is `python3 tools/gen_opcodes.py`, which needs a `clang-format` of the image's major
  version. The steps are in `README.md`, "Changing the opcode table".
- **Rejected alternatives** (`tasks/archive/2026/11/11/opcodes-x-macro-redesign.md`, Decisions):
  - a checksum manifest: a hand-edited committed file would pass it;
  - git hooks: the image build is the gate.

## Adding an engine port in a new language

A port gets its opcode table from the generator. It never parses the `.tsv` itself and never keeps a
hand-written copy.
1. **Write an emitter** in `tools/gen_opcodes.py`: a function `Table -> str` that returns the
   finished file. Model it on the closest existing one (`python_opcode_table`, `haskell_opcodes`,
   `rust_opcodes_table`).
   - It should give the port the rows in file order, with token numbers (`row.token`) and operand
     shapes.
   - It should give the decode table from `table.decode_rows()`, already sorted with one row per
     encoding.
   - It should give the operand-shape tags from `table.types`.
   - Use the C names (`TOK_ADD_OPCODE`, `RD_RS_RT_INST`), or a mechanical spelling of them such as
     Rust's `RdRsRtInst`, so the sources can be read side by side.
   - Write the file in final form if you can. If it must go through a formatter, the formatter has
     to be installed in the image, as `clang-format` is.
2. **Register the output** in `OUTPUTS`. The Dockerfile check and the meson test cover it from then
   on.
3. **Have the port read the generated module** for every lookup the C engine does on these tables:
   keyword to token, token to name and operand shape, token to encoding, and encoding to token. Use
   the decode table as given; do not reproduce `qsort` or `bsearch` order. The structural tokens
   256–263 stay hand-defined in the port, as in the existing three.
4. **Run `make generate`**, commit the generated file with the emitter, and add the file to this
   doc's table above.
5. **Follow the rest of the port rules** in `tasks/reference/engine-ports-design.md`. The toolchain
   goes in `entrypoint/05-install-ports.sh`, unconditionally, and the port runs the C engine's
   suites through `port/meson.build`.

When the C engine gains an instruction, add its row to `opcodes.tsv` and run `make generate`. That
updates every port's table at once. Each port's suite then fails until the port implements the new
instruction's execution, which is the signal for what to carry into each port.

## History

- **Until 2026-11-11, `include/opcodes.h` was a hand-written X-macro.** Each consumer defined
  `OP(name, sym, type, enc)` and included the file, so one list fed:
  - the `TOK_*` enum, which `tokens.h` built by including the file inside an enum body. That forced
    the file to hold nothing but comma-terminated rows;
  - the scanner's keyword table, the parser's operand-shape table, and `instruction.c`'s three
    tables, which were sorted with `qsort` at startup;
  - an unbuilt `src/dump-opcodes.c`.
- **The tree-sitter scripts and the three ports each parsed or reproduced it their own way,** with
  no staleness check: `extract-keywords.py`, `GenOpcodes.hs`, `gen_opcodes.c`, and a regular
  expression in the Python port.
- **The redesign** moved the rows to the data files, generated everything from them, and retired
  the X-macro, the enum-body `#include`, the startup sort and those per-consumer parsers.
- **An earlier evaluation (2026-10-16)** ruled out C's `_Generic` as a replacement. It dispatches on
  one expression's type and cannot iterate a table. That question no longer applies.
