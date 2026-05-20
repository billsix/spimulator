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

---

# Investigation results (2026-05-20)

Survey of `src/parser.c` (1714 lines, post-cleanup), `include/op.h`
(540 lines / 390 keywords), and the runtime-emit surface.  Numbers
in this section are measured, not estimated.

## Grammar surface (the input to a `grammar.js`)

| Surface | Count | Notes |
|---|---|---|
| Keywords in `op.h` | 390 | 41 directives + 49 pseudo-ops + 279 real opcodes + 12 noarg + others |
| Distinct grammar productions in `parser.c` | 46 | `parse_*` static functions |
| Instruction type dispatches (`parse_asm_code` switch) | ~25 | R3, R2sh, R1s, R2st, R2ds, R2td, I2, I2a, I1s, I1t, B1, B2, J, BC, MOVC, FP_R3, FP_R2ds, FP_R2ts, FP_CMP, FP_MOVC, FP_I2a, R3sh, NOARG, PSEUDO_OP |
| Directive dispatches (`parse_directive` switch) | 32 | 14 with real semantics + 18 sync-to-newline ignorers |
| Total `case` statements in parser.c | 114 | including operand-form distinguishing cases |

Tree-sitter grammar.js estimate: **300-500 lines** for the rules
themselves, plus a 390-entry token enumeration that mostly maps 1:1
from op.h.  Grammar would be smaller than the 1714-line parser.c
because the operand-shape logic that's currently nested `if`-trees
becomes declarative `choice(...)` rules.

## Runtime-call surface (the input to a tree-walker)

`parser.c` makes side-effecting calls to **23 distinct runtime
functions**:

```
r_type_inst, r_sh_type_inst, r_co_type_inst, r_cond_type_inst,
i_type_inst, i_type_inst_free, j_type_inst,
store_word, store_half, store_byte, store_double, store_float,
store_string,
record_label, make_label_global,
div_inst, mult_inst, set_le_inst, set_gt_inst, set_ge_inst, set_eq_inst,
nop_inst, trap_inst, branch_offset
```

These are the targets a tree-walker would need to drive after
tree-sitter parses.  Most map 1:1 from a grammar node type to a
runtime call; the pseudo-op expanders (`div_inst`, `mult_inst`,
the `set_*_inst` family) take a parameter and emit multiple
underlying instructions, so the walker's dispatch is essentially a
mirror of `parse_asm_code`'s switch.

Walker size estimate: **800-1200 lines** of C.  Smaller than
`parse_asm_code` itself (which has the operand-form-discrimination
logic inline) because tree-sitter's grammar already discriminated
the operand shapes before the walker runs.

## Total LoC swap

| Component | Today | Tree-sitter approach |
|---|---|---|
| Grammar | implicit in 1714-line parser.c | 300-500 line `grammar.js` |
| Lexer | 772-line `scanner.c` | tree-sitter's built-in lexer + ~50 lines of external scanner C |
| Tree walker / runtime bridge | (inline in parser.c) | 800-1200 lines of C |
| Generated parser tables | n/a | 5000-15000 lines of generated C (committed or regenerated) |
| **Total maintained** | **~2500 lines** | **~1500 lines + 10k generated** |

Net hand-maintained LoC shrinks by ~40%.  Total LoC (including
generated tables) grows ~5x.

## Build-system impact

To regenerate the parser tables, you need:
- Node.js (Dockerfile +90 MB)
- `npm install -g tree-sitter-cli` (a few MB)
- A `tree-sitter generate` step in `meson.build` that runs whenever
  `grammar.js` changes

Two options:
- **Commit the generated `parser.c`**: dev machines don't need
  Node, but the diff for any grammar change is enormous.  CI runs
  `tree-sitter generate` to verify the committed file matches.
- **Regenerate on every build**: dev machines need Node; the
  source tree stays small; `meson compile` adds a generator step.

Both are common patterns in projects that use tree-sitter.

## Effort breakdown

| Phase | Description | Effort |
|---|---|---|
| 0 | Write a minimal `grammar.js` covering integer R/I/J + .data/.text/.word/.asciiz + labels.  Test against /examples/01-helloworld.asm. | ~3 days |
| 1 | Extend grammar to cover the full keyword surface (390 entries from op.h).  Mostly mechanical; needs a small generator to convert op.h to grammar terminals. | ~3 days |
| 2 | Write the tree walker that drives spim's runtime emit functions.  Most cases mirror `parse_asm_code`. | ~5 days |
| 3 | Add to meson.build, decide commit-vs-regenerate, document in README, add Node to Dockerfile. | ~2 days |
| 4 | Build a parity harness comparing hand-parser to tree-sitter-walker output for every `/examples/*.asm` and `tests/tt.*.s`.  Iterate until byte-clean. | ~5 days |
| 5 | Cutover: flip the default parser, keep hand parser as `-parser=hand` fallback for a settling period (echoing Phase 5 of the bison migration), then remove. | ~3 days work + ~2 weeks wall clock |
| **Total** | | **~3-4 weeks of active work** |

Phase 4 (parity harness) is the critical path; it's where edge
cases in the grammar surface and where the walker's pseudo-op
expansion has to match the hand parser exactly.  Estimate could
balloon if the grammar misses subtle precedence/ambiguity cases.

## What's easy

- The keyword surface (op.h) is already structured for code
  generation — a 50-line Python script would emit the tree-sitter
  token list from `op.h`.
- The grammar has no left recursion, no precedence headaches; spim's
  expressions are simple (`EXPR → TERM (+ TERM)*`).
- The runtime emit surface is small (23 functions) and stable.
- Tree-sitter has built-in error recovery; the hand parser's manual
  `sync_to_nl()` calls would go away.

## What's hard

- **Pseudo-op expansion semantics**.  The hand parser expands `la
  $a0, foo` into `lui $at, %hi(foo); ori $a0, $at, %lo(foo)` *at
  parse time* by inline calling `i_type_inst_free`.  In a
  tree-sitter world, the tree has one `la` node and the walker
  expands it.  Same end result, but the timing differs — symbol
  resolution and `branch_offset` calls happen at different points
  in the call chain.  Risk of subtle behavioural divergence.
- **The `.set noat` / `noat_flag` toggle**.  This is parser state
  that changes which constructs are syntactically legal mid-file.
  Tree-sitter grammars can use external scanners for this, but it
  adds complexity.
- **Label fix-up across line boundaries**.  The hand parser
  maintains `this_line_labels` and applies `fix_current_label_address`
  retroactively when an alignment directive follows a bare label.
  This is parse-time state, not tree state — the walker would have
  to reconstruct it.
- **Forward references**.  Labels can be used before defined; the
  hand parser records uses, then `flush_local_labels` resolves them
  at end-of-file.  The walker has the same data flow but it's a
  second pass.
- **REPL command parser**.  `spim.c` uses the scanner (not the
  parser) to tokenise commands like `load "foo.s"` / `print $a0`.
  This wouldn't change with a tree-sitter parser, but the scanner
  has to stay for REPL use either way — there's no single-parser
  unification win.

## Q1 answered: what's the actual use case?

This is the load-bearing question.  Three plausible answers, each
implying different scope:

### Use case A: editor integration only (recommended)

Write `grammar.js` as a **sibling project** named
`tree-sitter-mips-spim`, publish on npm + crates.io.  Editors that
support tree-sitter (Helix, Neovim 0.5+, Zed, Emacs 29+ via
`treesit.el`, VS Code via wasm) get syntax highlighting and
structure-aware navigation for `.s` / `.asm` files.

**Spim itself doesn't change.**  No risk to the binary, no new
build dependency, no parity-harness work.  Just publish a grammar.

Effort: ~1 week to write grammar.js, test it, publish.

Wins:
- Students editing MIPS in any tree-sitter editor get
  professional-grade highlighting and motion.
- The grammar IS the documentation; readers can grep `grammar.js`
  for "what's the syntax for `.float`?" without spelunking
  `parser.c`.
- Zero risk of breaking spim.

This is the high-payoff / low-risk option.  Strongly recommended
as the first move regardless of any later decision.

### Use case B: better error recovery for the spim REPL

The hand parser bails on the first error per line (via
`sync_to_nl`) and the interactive student sees one error at a
time.  Tree-sitter would parse the whole file and report multiple
errors per build.

**But**: tree-sitter's `MISSING`/`UNEXPECTED` node recovery is
designed for editors that re-parse on every keystroke; spim's
batch mode would benefit, but the marginal value over "one error,
fix, re-run" is small in a teaching context.

This use case isn't compelling on its own.

### Use case C: replace the spim parser

Effort: 3-4 weeks as broken down above.  Risk: medium-high — the
parity harness has to be airtight.

Wins beyond use case A:
- Less hand-maintained code (~40% fewer lines if you don't count
  the generated parser).
- Same grammar drives spim AND editor integration; one source of
  truth.

Losses:
- New build dependency (Node.js).
- Generated-table noise in the source tree (or build-time gen).
- Throwing away ~2500 lines of just-cleaned-up parser/scanner code.
- The hand parser is a teaching artifact in its own right — a
  student reading `parser.c` can see exactly how recursive-descent
  parsing works.  Replacing it with a generated table-driven
  parser obscures that.

This use case is **not recommended for spim** specifically.  The
codebase is small enough that the hand parser earns its keep, and
the teaching context values readable parser code over generated
table compactness.

## Recommendation

**Do use case A.**  Write the grammar as a sibling project, publish
it, get editor integration as a free win.  Don't touch the spim
binary.

**Don't do use case C.**  The parser was just renamed, cleaned,
and modernised; the win from tree-sitter is real but doesn't
justify the rewrite cost for a single-instance teaching simulator.

If A is published and an unforeseen use case appears that needs C
(e.g. spim wants to embed as a library and consume `grammar.js`
results without going through the side-effecting parser), revisit
then.

## Out-of-task observation

The `tree-sitter-mips-spim` grammar would benefit several adjacent
projects (any MIPS assembly course materials, including `/examples`).
If Bill publishes it, the natural audience is computer-architecture
educators, not just spim users.

## First concrete step (revised)

If use case A is the answer:

1. Create a new git repo `tree-sitter-mips-spim` (separate from
   `/spimulator`).
2. Write a 50-line Python script that converts `op.h` to a
   `grammar.js` token list.
3. Write `grammar.js` rules for the 25 instruction-type
   discriminations + 14 real-semantic directives.  Iterate against
   `/examples/01-helloworld.asm` first.
4. `tree-sitter test` against the corpus in `tests/`.
5. Publish.

If use case C is ever the answer: re-read this file and triage
the "What's hard" section before committing.

