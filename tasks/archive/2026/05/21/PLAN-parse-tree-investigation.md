# Parse tree investigation plan

Motivated by: "is the current parser readable, would a parse tree
make it more readable, and would a parse tree be a better foundation
for a future GUI that visualizes memory cells being filled in as the
file assembles?"

This plan describes the **investigation work** that would answer the
question — not the implementation itself. Implementation comes only
if the investigation lands on "yes, switch to a parse tree."

---

## Preliminary read (already done in this session)

A spot-check of `src/parser.c`, `src/inst.c`, `src/data.c` confirms:

- The parser is **syntax-directed translation (SDT)**. Each
  per-instruction `parse_*` function calls an action helper like
  `r_type_inst(op, rd, rs, rt)`, `i_type_inst(...)`,
  `store_word(value)`, `store_string(...)`, etc. mid-parse.
- Those action helpers call `make_r_type_inst()` /
  `make_i_type_inst()` to allocate a small `instruction*` struct,
  then `store_instruction()` which writes the instruction directly
  into `text_seg[]` (or `k_text_seg[]`). For data directives,
  `store_byte/half/word/string` call `mem_write_byte/half/word`
  immediately.
- Per-line ephemeral structures exist (`instruction`, `imm_expr`,
  `addr_expr`) but they're 1:1 with memory cells and are placed
  into memory the moment they're constructed. There is **no
  whole-file AST**, no list of statements per line, no module-level
  data structure that survives `parse_line()`.
- Labels: `record_label()` enters the symbol table immediately.
  Forward references go on a per-label "uses" list, resolved when
  the label is later defined. This is the only piece of cross-line
  state that lives past a single `parse_line()` call.

So **question 1 is already settled**: current architecture is SDT;
there is no parse tree.

---

## What the investigation should answer

### Q2. Would building a parse tree be difficult? What are the advantages?

#### Investigation steps

1. **Catalog every action call site in `parser.c`** — the points
   where the parser today calls `r_type_inst` / `i_type_inst` /
   `j_type_inst` / `store_word` / `store_byte` / `store_half` /
   `store_string` / `r_sh_type_inst` / `r_cond_type_inst` /
   `r_co_type_inst` / `i_type_inst_full_word` / `align_text` /
   `align_data` / `record_label` / `make_label_global` /
   `set_data_alignment` / `record_inst_uses_symbol` /
   `record_data_uses_symbol`. Each becomes either an AST node or a
   side-effect attached to one.
2. **Design the AST node set.** Likely shape:
   - `ast_inst_r` (op, rd, rs, rt)
   - `ast_inst_i` (op, rt, rs, imm_expr)
   - `ast_inst_j` (op, addr_expr)
   - `ast_inst_shift` (op, rd, rt, shamt)
   - `ast_inst_fp_*` (a few variants)
   - `ast_data_byte/half/word/double/string` (value or list of values)
   - `ast_directive_align/space/asciiz/...` for the side-effect
     directives that don't directly emit bytes
   - `ast_label_def` (name, segment-local position)
   - `ast_segment` (`.text` / `.data` / `.kdata` / `.ktext`)
   - File-level node = ordered list of the above
3. **Identify the hard cases.** Three areas to look at carefully:
   - **Pseudo-op expansion.** Some single source lines emit multiple
     instructions today (`la` → `lui` + `ori`; `li` with large
     constant → `lui` + `ori`; `mul` immediate form → `ori $at` +
     `mul`). Two choices for the AST:
     - **Lossy:** AST stores the expanded form (multiple
       `ast_inst_*` nodes per source line). Loses the "this was an
       `la`" information unless we add an explicit `ast_pseudo` node
       that wraps its expansion children.
     - **Faithful:** AST stores a single `ast_pseudo` node with
       the original mnemonic; expansion happens at emit time.
       Better for the GUI (you can show "la expands to these
       instructions when assembled"). Slightly more code.
   - **Alignment side effects.** `.align N` and the implicit
     auto-alignment in `store_half/word/double` advance the data
     PC and may also retroactively shift labels defined earlier on
     the same line (`fix_current_label_address` in parser.c). The
     AST has to either (a) bake addresses in at parse time, or (b)
     compute addresses during the emit pass. Option (b) is cleaner
     architecturally but means labels in the symbol table get
     populated in the emit pass, not the parse pass.
   - **Forward references.** Currently
     `record_inst_uses_symbol`/`record_data_uses_symbol` add the
     instruction's location to the label's "uses" list at parse
     time. With an AST, the location isn't known until emit. Either
     (a) defer use-recording to emit (clean), or (b) do a two-pass
     emit: first compute addresses by walking the AST, then
     populate memory with resolved values (cleaner still, classic
     two-pass assembler).
4. **Sketch the emit pass.** A function `emit_ast(ast_file*)` that
   walks the AST and calls today's `store_instruction` /
   `mem_write_word` / etc. The existing action helpers
   (`r_type_inst`, `store_word`, ...) become the emit-pass leaves
   — minimal change to those.
5. **Estimate code delta.** Probable shape:
   - New: `include/ast.h` (~200 lines of struct definitions),
     `src/ast.c` (~400 lines of constructors + free), `src/emit.c`
     (~500 lines of AST walker calling existing actions).
   - Modified: `src/parser.c` (~600 lines changed — each `parse_*`
     returns/appends an AST node instead of calling actions).
   - Net: probably +500 to +800 lines total. Maintains the same
     surface for `inst.c`/`data.c`/`sym-tbl.c`.

#### Advantages to weigh against the cost

- **Two-pass assembly** is more conventional and would let us drop
  the per-label "uses list" forward-reference dance (the AST emit
  pass knows all label addresses before it writes anything).
- **Static analysis** becomes possible (range checks, unused-label
  warnings, dead-code detection) as a tree walk between parse and
  emit.
- **Error messages** can be more precise — the AST node carries
  source line + column, even after the parse phase has moved on.
- **Replayable emission** for a GUI (see Q4).
- **Pseudo-op visibility** as described above — the GUI or a
  `-listing` flag can show "this `la` expanded to these two
  instructions."
- **Testability** — the parse pass can be unit-tested by
  comparing AST shapes, without invoking the simulator.

#### Drawbacks

- More code (estimate above).
- A second representation to keep in sync (token stream → AST →
  memory) instead of (token stream → memory).
- Memory cost: the AST is in heap for a moment between parse and
  emit. For 50,000-line programs that's not nothing. (Bound: ~50
  bytes per node × tens-of-thousands of nodes = a few MB. Fine.)
- Loses the "1-pass nature" that's spim's traditional simplicity
  pitch.

### Q3. Can it be done while preserving behavior?

#### Investigation steps

1. **Behavioral inventory.** Catalog every observable spim behavior
   that touches the parser:
   - Symbol table population order
   - Forward reference resolution timing
   - Error message wording and line numbers
   - Alignment rules (esp. label-on-same-line-as-directive)
   - Pseudo-op expansion details (which expansions happen, what
     temp registers they use)
   - `print_symbols` output ordering
   - `.set noreorder` / `.set noat` semantics (currently ignored;
     would they stay ignored?)
   - Error recovery — currently `sync_to_nl` skips the bad line
     and continues. Same semantics in two-pass?
2. **Regression test coverage.** The 17 tests in `meson test`
   plus the golden `tt.explain.expected` cover a lot. List which
   parser corners they exercise and which they don't.
   Specifically check: forward references across `.text` and
   `.data`, large `li` constants, branch ranges, pseudo-ops with
   labels in immediates, alignment edge cases.
3. **Decide on the migration path.** Two viable shapes:
   - **Big-bang.** Write the AST-based parser in parallel,
     switch over once it passes the regression suite, delete the
     old parser. Same approach used for the bison-to-handwritten
     migration documented in `handwritten-parser-migration.md`.
   - **Incremental.** Add `-parser=ast` flag, keep both paths
     alive, gradually move users.
   The earlier bison-to-handwritten migration used big-bang. For
   a similar-size refactor that's probably the right call again.

The answer to "can it be done preserving behavior" is almost
certainly yes — assemblers have been written both ways for 60
years, and the test suite gives a solid behavioral fence. The
investigation question is more "what edge cases would bite?"

### Q4. For a future GUI showing memory cells being filled in, which is better?

This is the question the rest hangs on. The GUI vision: user opens
an .asm file, scrubs through assembly time, watches each `text_seg`
and `data_seg` cell flip from empty/highlighted to its assembled
value, with the source line highlighted that produced each write.

#### Three possible architectures for the GUI

**Architecture A — keep SDT, instrument `mem_write_*`.**
Add a hook function pointer (`void (*mem_write_observer)(addr, value, kind)`)
that fires every time the simulator writes a memory cell during
assembly. The GUI records the event stream. Lightweight; no parser
change. ~50 lines of code. Limitations:
- Can't show "what's about to happen" — only "what just did."
- Can't show pseudo-op expansion *structurally* (would have to
  infer it from N consecutive writes within the same source line).
- Can't show the parse phase as a separate animation step.
- Re-running the animation means re-parsing.

**Architecture B — switch to parse tree, emit instrumented.**
Parser builds AST. GUI receives the AST. User scrubs through emit
phase; each AST node has known source-line provenance. Two-phase
animation (parse → emit). Heavy refactor — see Q2. Advantages:
- Static "preview" of what will happen before emit.
- Pseudo-op nodes carry their expansion children, so the GUI can
  show "this is one source line that becomes N memory writes" as
  a tree.
- Scrubbing is just walking the AST; no re-parse.
- "Hover a memory cell, see its source AST node" is a tree
  lookup.

**Architecture C — keep SDT, add an event log + source span on
each `instruction*`.**
Halfway point: don't refactor the parser, but capture during the
parse pass an ordered event log:
```
[parse_line 5] label "loop" defined at 0x00400020
[parse_line 5] wrote text@0x00400020: addi $t0, $zero, 0  (0x20080000)
[parse_line 5] wrote text@0x00400024: ...
```
The GUI consumes the log post-parse. Same source-correlation
guarantee as B, but without the data-structure cost. ~100 lines
of code (an event log struct + calls from the existing actions).
Limitations:
- No "preview before emit" — the log IS the emit.
- Pseudo-op grouping has to be done by source-line correlation,
  not by tree structure.
- For static analysis or other AST-driven features later, you'd
  still need to do B.

#### Recommendation framing

For the **GUI alone**, Architecture C (instrumented SDT with event
log) is the highest leverage. Implementation cost is a fraction of
B, and the user-visible features for memory-cell animation are
nearly identical.

For a **broader teaching tool** — where the GUI is one of several
features (static analysis, listing files, pre-emit validation,
better error messages, pluggable expansion strategies for pseudo-
ops, "show me the AST" tab) — Architecture B is the better long-
term shape.

So the decision really is: is the GUI a one-off, or the start of
a richer pedagogical surface? That's the question for the user.

---

## Proposed deliverables of the investigation

If you approve the investigation:

1. **Action-site catalog** (a markdown table in this folder) listing
   every action-call in `parser.c` and what AST node would absorb
   it.
2. **AST node set sketch** (a header file or markdown) showing
   the proposed `ast_*` structs.
3. **Hard-case writeup**: pseudo-op expansion, alignment side
   effects, forward references — concrete plan for each.
4. **GUI architecture comparison** — a short doc comparing
   Architectures A/B/C with effort estimates and feature matrices,
   to drive the actual decision.
5. **Recommendation** at the end: "do A, do B, do C, or don't do
   this."

Each is read-only research; no code changes. Total effort: 1 day
of focused reading and writing.

---

## Order of operations

1. Spot-check parser.c action-call sites (~2 hours).
2. Sketch AST node set + sample AST for a 10-line program (~2 hours).
3. Walk the three hard cases above (~3 hours).
4. Draft the GUI architecture comparison (~1 hour).
5. Write the recommendation (~1 hour).

If after step 4 the recommendation is "Architecture C is sufficient,"
the rest of the parse-tree investigation collapses. That's a
plausible outcome and would save weeks of refactoring.
