# Task: investigate rewriting the parser in tree-sitter

## Goal

Assess whether the hand-written recursive-descent parser (currently
~1700 lines in `src/parser.c`, ~770 lines in `src/scanner.c`)
could be replaced by a [tree-sitter](https://tree-sitter.github.io/)
grammar, and whether doing so would be a net win.

The hand-written parser is the result of the Phase 5 migration
that just landed (see
[`handwritten-parser-migration.md`](handwritten-parser-migration.md)).
This task DOESN'T propose to discard that work — it just
investigates a possible next move.

## Why tree-sitter is potentially interesting

- **Editor / IDE integration**: tree-sitter grammars produce
  parsers that can drive syntax highlighting, structural
  navigation, and folds in editors that support tree-sitter
  (Emacs, Neovim, Helix, Zed, VS Code via wasm).  Bill teaches
  with this codebase; students editing MIPS in a tree-sitter-aware
  editor would get real-time syntax highlighting and structure-aware
  motion for free.
- **Error recovery**: tree-sitter's GLR-style parser keeps
  parsing past errors, producing a partial syntax tree.  The
  current parser bails on the first error and synthesises
  TOK_NL to keep going at line boundaries.  Tree-sitter's
  per-node recovery is more graceful for editor use.
- **Grammar as documentation**: a `grammar.js` file is a
  declarative spec.  Today the grammar is implicit in the
  hand-written dispatch in parser.c.

## Why tree-sitter might be wrong for spim

- **Tree-sitter doesn't emit code** in spim's runtime style.  Its
  C output is a parse tree, not the side-effecting
  `r_type_inst()`/`store_word()`/`record_label()` calls that the
  current parser makes inline.  spim would have to walk the tree-
  sitter parse tree in a second pass to drive its existing
  emitters.  That second pass is roughly the size of the current
  parser.
- **Two-pass changes pseudo-op expansion**.  The current parser
  expands `la $a0, foo` into `lui $at, %hi(foo); ori $a0, $at,
  %lo(foo)` AT PARSE TIME by directly calling
  `i_type_inst_free(TOK_ORI_OP, ...)` from the parser action.
  In a tree-sitter world, the tree has one node "la" and the
  expansion happens in the walker.  Same end result, more code
  to write.
- **Build dependency**: tree-sitter generates the parser via
  a Node.js / npm toolchain (`tree-sitter generate`), then
  compiles the generated C.  Spim today builds with just meson +
  clang + libedit.  Adding tree-sitter means adding Node.js to
  every developer's machine and to the Docker image.
- **Recently completed work**: the hand parser is fresh,
  working, parity-clean against every test, and rewriting it
  would throw away ~2500 lines of just-cleaned-up code.

## Investigation questions

Before doing any work, answer these in writing:

### 1. What's the actual use case?

- **Editor integration only**?  Then tree-sitter can be added
  as a sibling project, not a replacement.  Write `grammar.js`
  and publish it; the spim binary keeps using the hand parser.
  Best of both worlds, no risk to spim.
- **Better error recovery for the REPL / interactive teaching
  mode**?  Worth comparing how tree-sitter handles the kinds of
  errors students actually make (typo'd opcodes, missing commas,
  wrong number of operands) vs. the current parser.
- **Faster iteration on grammar changes**?  Tree-sitter's
  `grammar.js` IS more readable than recursive-descent C, but
  the spim grammar is small and stable — when did we last
  change it?

### 2. What does the spim grammar actually look like in
   tree-sitter notation?

Sketch `grammar.js` for the spim language.  Get a feel for the
size and complexity.  The MIPS instruction set is ~390 opcodes
(see `include/op.h`); each is essentially a token + operand
pattern.

Estimate: ~300-500 lines of `grammar.js`.  Probably smaller than
the current 1700-line parser.c.

### 3. What's the bridge from tree-sitter parse tree to spim
   runtime calls?

The current parser does its side-effecting work inline (a parser
action calls `r_type_inst()` directly).  A tree-sitter port would
walk the parse tree post-parse and dispatch.

Sketch the walker function.  Estimate its size.

### 4. What's the build-system impact?

- Does the tree-sitter generated parser get committed to the
  repo, or regenerated on every build?
- If regenerated, every developer needs Node.js.  Acceptable?
- If committed, the parser tables become a multi-thousand-line
  blob of generated C in the source tree.  Acceptable?

### 5. What's the cost to throw away if it doesn't work?

Hand parser is in `src/parser.c` + `src/scanner.c`.  If
tree-sitter is attempted in a branch and abandoned, what's
recoverable?

## Decision gate

Don't start a port without answering Q1.  If the answer is
"editor integration only", build the grammar as a sibling
project — not a replacement — and skip everything below.

If the answer is something more ambitious (replace the spim
parser), then:
1. Write `grammar.js` (1 week).
2. Write the parse-tree walker that drives spim's runtime
   emitters (1-2 weeks).
3. Build a parity harness comparing hand-parser output to
   tree-sitter-derived output on the existing tt.*.s + /examples
   corpus.
4. Only authorize the cutover if parity is byte-clean.

## Out of scope

- Replacing the scanner only.  Tree-sitter is a parser
  generator; the scanner is small and self-contained, no win
  from porting it alone.
- Replacing the REPL command parser.  That's a
  hand-written token-by-token state machine in `spim.c`; not
  the assembler grammar.
- Anything tree-sitter-related that requires writing /
  maintaining a Node.js toolchain in the Docker image, unless
  there's a clear pedagogical win.

## First concrete step

Answer Q1 (what's the actual use case).  Write the answer in
this file.  Then decide.
