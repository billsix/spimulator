# The `opcodes.h` OP X-macro — how the single-source-of-truth keyword table works

**Reference document** — read before touching `include/opcodes.h`, `include/opcode-types.h`,
`include/tokens.h`, or any of the keyword/opcode tables derived from them, and before evaluating a
redesign (`tasks/opcodes-x-macro-redesign.md`). States how the mechanism works today and the design
analysis behind it. Durable; never archived; update in place. (Written 2026-10-16 — William Emerison
Six <billsix@gmail.com>.)

## What it is

`include/opcodes.h` is the **single source of truth** for every MIPS instruction, assembler
pseudo-instruction, assembler directive, and spim REPL command the simulator knows. It is an
**X-macro file**: 381 rows, each a call to a macro `OP()` that the file itself never defines —

```c
OP(".align",  TOK_ALIGN_DIRECTIVE, ASM_DIR,        -1)
OP("add",     TOK_ADD_OPCODE,      RD_RS_RT_INST,   0x20)
...
```

The four columns are: **(1)** the textual mnemonic the student writes, **(2)** the token symbol
(`TOK_*`), **(3)** an operand-shape type tag (`op_type`, from `opcode-types.h` — e.g.
`RD_RS_RT_INST` for `add rd, rs, rt`), **(4)** the binary encoding (`-1` for rows with no encoding:
directives, pseudo-ops, REPL commands). Rows are kept in alphabetical order; adding an instruction is
one row, and every derived table updates with it.

## How a consumer uses it

Each consumer **`#define`s its own `OP()`** to project out the columns it wants, then `#include`s
`opcodes.h`, so the same 381 rows expand into whatever shape that consumer needs. Verified consumer
sites (identified by their `OP()` expansion, which is stable even as line numbers drift):

| Consumer | `OP(...)` expands each row to | Builds |
| --- | --- | --- |
| `include/tokens.h` | `sym,` | the `TOK_*` enumerator list |
| `src/scanner.c` | `{NAME, OPCODE, TYPE},` | the keyword lookup table |
| `src/parser.c` | `{OPCODE, TYPE},` | the op-type table |
| `src/instruction.c` (×3) | `{NAME, OPCODE, TYPE}` / `{NAME, I_OPCODE, (int)A_OPCODE}` / `{NAME, (int)A_OPCODE, (int)I_OPCODE}` | three instruction lookup tables |
| `src/dump-opcodes.c` | `{a, d},` | a standalone mnemonic/encoding dump |
| `tree-sitter/scripts/extract-keywords.py` | (parsed externally, not via cpp) | the editor-grammar keyword lists |

The last row is the exception: the tree-sitter grammar does **not** `#include` the file; a Python
script reads it as text (`extract-keywords.py` → `build-keyword-lists.py` → `grammar.js`). So there is
already one out-of-tree consumer of this table.

## The two companion files, and the one genuinely odd constraint

- **`include/opcode-types.h`** holds the `op_type` enum (the operand-shape tags, `uint8_t`-backed,
  values 0..42). It is a normal guarded header included at file scope by the consumers that need the
  tag names visible.
- **`include/tokens.h`** does the unusual thing: it `#include`s `opcodes.h` **inside the body of an
  `enum : int32_t { ... }`**, so each `OP(...) -> sym,` row becomes an enumerator directly between the
  braces.

That one use dictates the three constraints `opcodes.h` documents at its top and must keep:

1. **No header guard** — it is re-included many times per translation unit with different `OP()`.
2. **No typedefs and no preprocessor directives other than the `OP()` rows** — anything that is not a
   comma-terminated expression is a syntax error inside the `tokens.h` enum body. This is why the
   `op_type` typedef lives in `opcode-types.h` and not in `opcodes.h`.
3. **The type-tag names are defined elsewhere** (`opcode-types.h`); a consumer that references them
   must include that header at file scope first.

The include-inside-an-enum-body trick in constraint (2) is the fragile, surprising part. It works and
is documented, but it is the reason the file cannot carry any structure of its own.

> **Build gate:** any change to `opcodes.h` or `opcode-types.h` must be verified with
> `BUILD_TREE_SITTER=1` — a `BUILD_TREE_SITTER=0` image never runs the keyword pipeline that consumes
> the mnemonic and type-tag columns (see `CLAUDE.md` §Conventions).

## Design analysis — could it be done differently?

### `_Generic` does not apply

A recurring instinct is that C11/C23 type-generic selection (`_Generic`) could replace this. It
cannot, and the reason is worth recording so it is not re-proposed. `_Generic` dispatches on the
**type of a single expression** at compile time — it is the tool for writing one type-generic function
(the way `<stdbit.h>`'s `stdc_leading_zeros` selects per argument type). The OP macro's job is the
orthogonal one of **iterating a data table to generate code** — 381 enumerators and table rows, across
several translation units. `_Generic` has no iteration and cannot emit declarations, enumerators, or
table rows. No C23 preprocessor feature replaces X-macro list expansion.

### The real axes

1. **Keep the X-macro** (status quo). It is a well-understood idiom, self-contained in the C build
   with no generator step and no generated files to manage, and it is correct-by-construction: every
   table is literally the same 381 rows, so they cannot drift. The cost is the `tokens.h`
   include-inside-enum trick and the resulting "no structure allowed" rule on `opcodes.h`.
2. **Move to external code generation** — the direction the tree-sitter keyword pipeline already
   takes. A generator reads the table (either `opcodes.h` as today, or a cleaner data source such as a
   TSV/JSON row file) and emits plain, greppable, debuggable generated C (the `TOK_*` enum, the
   keyword and opcode tables), retiring the enum-body `#include` entirely. The cost is a build-time
   generator plus the generated-file discipline in `github.com/billsix/runClaudeInContainer:tasks/reference/codegen-conventions.md`
   (never hand-edit the output, a parity harness proving the generated tables are byte-identical to
   today's, and a run-to-run determinism gate).

Neither is clearly better: (1) trades a weird-but-contained preprocessor idiom for zero build
machinery; (2) trades build machinery for ordinary C with no preprocessor contortions and a single
textual source already shared with the grammar. The decision, with its rationale, belongs in
`tasks/opcodes-x-macro-redesign.md`.

## Cross-links

- `include/opcodes.h` (the table + its own top-of-file X-macro primer), `include/opcode-types.h`,
  `include/tokens.h`; consumers `src/scanner.c`, `src/parser.c`, `src/instruction.c`,
  `src/dump-opcodes.c`.
- `tree-sitter/scripts/extract-keywords.py` / `build-keyword-lists.py` — the existing external
  consumer and the `BUILD_TREE_SITTER=1` gate.
- `github.com/billsix/runClaudeInContainer:tasks/reference/codegen-conventions.md` — the rules any external-codegen redesign must follow.
- `tasks/opcodes-x-macro-redesign.md` — the investigate-and-decide task.
- `tasks/archive/2026/10/16/c23-modernization-pass2.md` — the simulator-core modernization this was split out of.
