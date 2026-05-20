# tree-sitter-mips-spim

[tree-sitter](https://tree-sitter.github.io/) grammar for MIPS
assembly, **spim flavour** — the dialect accepted by James
Larus's [spim](https://spimsimulator.sourceforge.net/) and Bill
Six's [spimulator](https://github.com/billsix/spimulator) fork.

This grammar lives as a subdirectory of the spimulator repo
(`tree-sitter/`) so the keyword surface stays in lockstep with
`include/op.h`.  It's for **editor integration only** — syntax
highlighting, structural navigation, code folds in editors that
support tree-sitter (Helix, Neovim 0.5+, Zed, Emacs 29+ via
`treesit.el`, VS Code via wasm).  It does NOT replace the
spimulator binary's own parser; see
[`../tasks/investigate-tree-sitter.md`](../tasks/investigate-tree-sitter.md)
for the rationale behind that decision.

## Coverage

| Corpus | Files | Clean parse |
|---|---:|---:|
| [`/examples`](https://github.com/billsix/examples) (MIPS teaching demos) | 46 | **44** |
| `tree-sitter test` corpus | 5 | **5** |

The 2 `/examples` files that don't parse cleanly
(`24-get-char-from-user-*.asm`) use spim's comma-less
operand syntax (`li $v0 12` instead of `li $v0, 12`),
which is a documented teaching-bug demo.

## Quick start

### From inside the spimulator repo

```sh
cd spimulator/tree-sitter
npm install          # one-time, pulls tree-sitter-cli
make                 # generate + test
tree-sitter parse /path/to/some.asm
```

### Editor integration

Each editor has its own tree-sitter integration story; consult
the editor's docs.  The grammar's `package.json` declares the
default `file-types` (`.s`, `.asm`, `.mips`) and the location of
the `queries/highlights.scm` query.

## Keyword surface

The grammar's keyword list (381 entries: 41 directives, 49
pseudo-ops, 291 real opcodes across 25 instruction-type
categories) is generated from spim's `include/op.h` X-macro
table via `scripts/extract-keywords.py` → `scripts/keywords.json`
→ `scripts/build-keyword-lists.py` → `scripts/keyword-lists.js`.

To regenerate after `../include/op.h` changes:

```sh
make    # extract-keywords → keyword-lists → tree-sitter generate → tree-sitter test
```

The Makefile hardcodes `../include/op.h` as the source of truth.

## Grammar shape

Top-level: `source_file` → repeat of lines.  Each line is an
optional sequence of labels followed by an optional directive,
instruction, or constant definition.

```
source_file
├── label_def       (NAME ':')
├── constant_def    (NAME '=' EXPR)
├── directive       (.data | .text | .word EXPR_LIST | ...)
└── instruction
    ├── _inst_r3        op rd, rs, rt
    ├── _inst_r2sh      op rd, rt, shamt
    ├── _inst_i2        op rt, rs, imm
    ├── _inst_i2a       op rt, addr
    ├── _inst_b1        op rs, label
    ├── _inst_b2        op rs, rt, label
    ├── _inst_j         op target
    ├── _inst_fp_r3     op fd, fs, ft
    ├── ... (23 more, one per dispatch kind)
    └── _inst_pseudo    op operand* (la, li, move, ...)
```

The grammar covers spim's 25 instruction-format categories
plus 14 real-semantic directives.  Pseudo-ops are caught by a
single rule with a free-form operand list — semantic dispatch
(which pseudo-op expands to what) is left to consumers since
this grammar's purpose is editor display, not assembly.

## Known limitations

- **Comma-less operands**: spim's parser accepts `li $v0 12`
  alongside `li $v0, 12`.  This grammar requires the commas.
  Two `/examples` files trip on this.
- **`.set noat` semantics**: this grammar parses `.set noat`
  as a directive but does NOT track the `noat_flag` state that
  makes `$at` syntactically legal mid-file under spim's parser.
  For editor highlighting this is fine — `$at` is always
  rendered as a register either way.
- **External scanner**: not used.  Adding one would let us
  match the `\001` EOF sentinel and label-fixup-across-lines
  semantics that spim's hand parser implements, but neither
  matters for editor display.

## Why not replace spim's parser

The investigation lives at
[`tasks/investigate-tree-sitter.md`](https://github.com/billsix/spimulator/blob/master/tasks/investigate-tree-sitter.md)
in the spimulator repo.  Short version: spim's hand parser is
1714 lines of readable recursive-descent C that doubles as a
teaching artifact, the runtime emit functions are tightly
coupled to parse-time state (pseudo-op expansion, label
fix-up), and the build-system cost of adding Node.js +
tree-sitter-cli to spim's dev loop outweighs the marginal win.
Tree-sitter is the right tool for editor display; spim's hand
parser is the right tool for spim.

## License

BSD-3-Clause.  See `../LICENSE`.
