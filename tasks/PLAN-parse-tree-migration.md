# Parse-tree migration plan (C → B)

End state: spim's parser produces an explicit AST, the emit pass walks
the AST in two passes (symbols, then code), pseudo-ops are first-class
AST nodes that carry their expansion as children, and three teaching
flags (`-print-ast`, `-show-expansion`, plus the existing `-explain`)
form a coherent surface that lets a student see the assembler's
internal mechanics.

Path: event log first (Architecture C, cheap, immediately useful),
then AST (Architecture B, the real destination), then pseudo-op
first-class representation, then teaching surfaces.

Companion to `PLAN-parse-tree-investigation.md` — that doc decided
*which* destination; this one is the *route* to get there.

---

## Guiding principles

- **Regression suite stays green at every commit.** The 17 tests
  in `meson test` + the `tt.explain.expected` golden file are
  the behavioral fence. No phase merges without all of them
  passing.
- **Each phase ships independently.** A phase landing should
  leave spim in a fully-working state, never half-done. If we
  stop after Phase 1, spim is still better than today. If we
  stop after Phase 2, still better. And so on.
- **Old paths stay alive during transition.** Phase 2 introduces
  a `-parser=ast` flag and keeps the SDT path as default until
  the AST path passes everything; then the default flips; only
  later does the old SDT code get deleted. Same shape as the
  bison-to-handwritten migration documented in
  `handwritten-parser-migration.md`.
- **The teaching surfaces are the payoff, not the AST itself.**
  Don't build infrastructure that won't be used by `-print-ast`,
  `-show-expansion`, the GUI, or a static-analysis exercise.

---

## Phase 1 — Event log (Architecture C)

**Goal:** capture an ordered, source-correlated record of everything
the assembler does, without changing the parser architecture.

**Why first:** small (~1-2 days), independently useful for the GUI
prototype, gives us a behavioral oracle for Phase 2 (any AST-driven
emit must produce the same event log).

### Work items

1. **Define the event type.**
   New `include/asm_event.h`:
   ```c
   typedef enum {
     AE_LABEL_DEF,        /* a label became defined at addr */
     AE_TEXT_INST,        /* an instruction got written at addr */
     AE_DATA_BYTE,
     AE_DATA_HALF,
     AE_DATA_WORD,
     AE_DATA_DOUBLE,
     AE_DATA_STRING,
     AE_ALIGN,            /* PC advanced for alignment */
     AE_SEG_CHANGE,       /* .text / .data / .ktext / .kdata */
     AE_FORWARD_REF,      /* an instruction noted use of unresolved sym */
     AE_FORWARD_RESOLVED, /* forward ref got patched */
   } asm_event_kind;

   typedef struct {
     asm_event_kind kind;
     int source_line;
     const char* source_file;
     mem_addr addr;
     /* per-kind payload union */
     ...
   } asm_event;
   ```

2. **Add a single observer hook** to `inst.c` / `data.c`:
   ```c
   typedef void (*asm_event_observer)(const asm_event*);
   extern asm_event_observer asm_observer;
   ```
   Default observer is no-op; tooling installs its own.

3. **Instrument the action helpers** to fire events:
   `store_instruction`, `store_byte`, `store_half`, `store_word`,
   `store_double`, `store_string`, `record_label`, `align_text`,
   `align_data`, `record_inst_uses_symbol`,
   `record_data_uses_symbol`, `resolve_label_uses`. ~12 call
   sites, ~3 lines each.

4. **Source line propagation.** Each parsed line already knows
   `line_no` (from scanner.c) and `input_file_name` (from
   parser.c). The observer gets both with each event.

5. **Add `-listing FILE` flag** to spim.c. Installs an observer
   that writes events as human-readable text to FILE:
   ```
   line 12  text  0x00400020  addi $t0, $zero, 0       0x20080000
   line 12  text  0x00400024  jal  loop                <unresolved: loop>
   line 18  label  loop  defined at 0x00400040
                          resolved forward ref @ 0x00400024
   line 19  text  0x00400040  add  $v0, $a0, $a1       0x00853020
   line 20  data  0x10010000  word 0x000000ff
   ```

6. **Regression tests.**
   - `tests/tt.listing.s` — small program covering one of each
     event kind.
   - `tests/tt.listing.expected` — golden listing.
   - Run target wired into `meson.build`'s regression array.

### Deliverable
spim has a `-listing` flag that produces a complete trace of
assembly. The first GUI prototype consumes this. Phase 2's AST
emit pass must reproduce byte-identical listings against the
same input.

**Effort:** 1-2 days.

---

## Phase 2 — AST data structure + parser refactor (Architecture B core)

**Goal:** parser produces an AST; emit walks the AST. End-state
behavior identical to today.

Subdivided because this is the big change.

### Phase 2a — Action-site catalog

**Output:** `tasks/parse-actions-catalog.md` — a table listing every
call in `parser.c` to `r_type_inst` / `i_type_inst` / `j_type_inst`
/ `store_word` / etc. with:
  - Call site (file:line)
  - What information it carries
  - What AST node type would absorb it

No code change; 2-3 hours of careful reading.

### Phase 2b — AST type definitions

**Output:** `include/ast.h` with the node set:

```c
typedef enum {
  AST_INST_R,          /* op rd, rs, rt */
  AST_INST_R_SHIFT,    /* op rd, rt, shamt */
  AST_INST_I,          /* op rt, rs, imm */
  AST_INST_J,          /* op target_label */
  AST_INST_FP_R,
  AST_INST_FP_I,
  AST_INST_FP_BRANCH,
  AST_INST_FP_COMPARE,
  AST_PSEUDO,          /* see Phase 3 */

  AST_DATA_BYTE,
  AST_DATA_HALF,
  AST_DATA_WORD,
  AST_DATA_DOUBLE,
  AST_DATA_STRING,
  AST_DATA_SPACE,

  AST_DIR_TEXT,
  AST_DIR_DATA,
  AST_DIR_KTEXT,
  AST_DIR_KDATA,
  AST_DIR_ALIGN,
  AST_DIR_GLOBL,
  AST_DIR_EXTERN,
  AST_DIR_SET,         /* mostly ignored today */

  AST_LABEL_DEF,
  AST_FILE,            /* root */
} ast_kind;

typedef struct ast_node {
  ast_kind kind;
  int source_line;
  /* union of per-kind payload */
  ...
  struct ast_node* next;   /* for the file-level list */
  struct ast_node* child;  /* for AST_PSEUDO and AST_FILE */
} ast_node;
```

Pure additions to the codebase; no behavior change.

**Effort:** 1 day for design + writing the header + constructors.

### Phase 2c — Constructors + free + visitor

**Output:** `src/ast.c` with `ast_make_inst_r(...)` etc.,
`ast_free(node)`, and `ast_visit(file_root, callback)` for the
emit pass to use.

Still no parser change. Compiles and links into spim but isn't
called yet.

**Effort:** 1 day.

### Phase 2d — Parser produces AST

**The big one.** Modify each `parse_*` function in `parser.c`:
  - Instead of calling `r_type_inst(op, rd, rs, rt)`, return or
    append `ast_make_inst_r(op, rd, rs, rt)`.
  - Each `parse_line` appends its node to a growing file-level
    `ast_node*` list.
  - `parse_file` returns the file root.

Add a new `src/emit.c` with `void emit_ast(ast_node* file_root)`
that walks the AST and calls the existing action helpers
(`r_type_inst`, `store_word`, etc.). Those helpers stay
**unchanged** — they remain the canonical "write to memory"
primitives.

**Gating flag:** `-parser=ast` selects the new path; old path is
default. Lets us run both side-by-side and diff their event logs.

Hardest sub-parts (estimated):
  - Pseudo-op expansion sites in `parse_r3`, `parse_li`, `parse_la`.
    For Phase 2 these still expand into multiple AST nodes (the
    parse_la function appends 2 nodes instead of 1). Phase 3
    converts these to `AST_PSEUDO` wrappers.
  - Label fix-up during `.align` — `fix_current_label_address`'s
    retroactive update of labels defined earlier on the same line.
    The AST version moves this into the symbol-table pass.
  - Forward references — record on the AST node, not in the
    label's uses-list at parse time. Resolve during emit.

**Migration testing:** at each step, run the regression suite
with `-parser=ast` and confirm green. The listing-file from
Phase 1 must be byte-identical under both parsers.

**Effort:** 5-8 days (the bulk of the project). 

### Phase 2e — Two-pass emit

Replace the one-pass emit from 2d with the canonical two-pass:

  **Pass 1:** walk the AST, assigning addresses to labels and
  data items. Populate symbol table. No memory writes.

  **Pass 2:** walk the AST, calling the action helpers
  (`r_type_inst`, `store_word`, ...) to actually write memory.
  All labels are now resolved.

This eliminates the per-label "uses list" + retroactive fix-up
mechanism. Forward references become trivial — they're just
labels the pass-1 symbol table already knows about by pass 2.

Regression tests stay green; listing file stays byte-identical.

**Effort:** 2-3 days.

### Phase 2f — Flip the default, then delete

Once the AST path passes everything for a sustained period
(say, two weeks of active development):
  - Make `-parser=ast` the default.
  - Keep the old SDT path under `-parser=sdt` for one release as
    fallback.
  - Following release: delete the old SDT path.

The SDT path was a hand-written recursive-descent parser; it
doesn't lose information being deleted. The AST parser is the
superset.

**Effort:** 1-2 days.

### Phase 2 total effort
**~10-15 days of focused work.**

---

## Phase 3 — Pseudo-ops as first-class AST nodes

**Goal:** every pseudo-op (`la`, `li` with large constants,
`mul` with immediate, `bge`, `ble`, `bgt`, `blt`, `seq`, `sne`,
`move`, `not`, `neg`, etc.) becomes a single `AST_PSEUDO` node
whose children are the real instructions it expands to.

### Work items

1. **Move pseudo-op expansion out of parser.c into a new pass.**
   The parser produces `AST_PSEUDO` nodes; a pseudo-expansion
   pass between parse and emit replaces each pseudo node with
   a wrapper containing its expansion as children. The wrapper
   retains the original mnemonic for display purposes.

2. **The pseudo-info table from explain.c** (used by the teaching
   mode) becomes the authoritative source. Today it has 40
   entries for narration; we extend it to also describe the
   expansion shape. Single source of truth for "what does `la`
   become?"

3. **`-show-expansion` flag.** Prints the AST with pseudo nodes
   expanded, indented:
   ```
   line 14: la $a0, msg
            lui $a0, hi(msg)
            ori $a0, $a0, lo(msg)

   line 15: li $t0, 100000
            lui $t0, hi(100000)
            ori $t0, $t0, lo(100000)

   line 16: addi $t1, $t0, 1
   ```

4. **Listing-file integration.** Phase 1's `-listing` flag gets
   a new event type `AE_PSEUDO_EXPAND` that signals the
   beginning of a pseudo expansion in the trace:
   ```
   line 14  pseudo  la $a0, msg
   line 14    text  0x00400020  lui $a0, 0x1001       0x3c041001
   line 14    text  0x00400024  ori $a0, $a0, 0x0040  0x34840040
   ```

5. **Tests.** Golden expansion outputs for each pseudo-op.

**Effort:** 3-5 days.

---

## Phase 4 — Teaching surfaces

**Goal:** three teaching modes that together cover assembly time
+ runtime + structure:

  - `-print-ast` — dump the AST structurally
  - `-show-expansion` — show pseudo-op rewrites (from Phase 3)
  - `-explain` — narrate runtime execution (existing)

### Work items

1. **`-print-ast`** — call `ast_visit` with a printer that emits
   the tree as indented text. Useful both for students and for
   debugging the assembler. Probably also serializable to JSON
   for the GUI later.

2. **Documentation in the curriculum.**
   - A new `/examples` doc: `TEACHING-ASSEMBLER-INTERNALS.md`
     walking a student through the AST of a small program.
   - Pointer from `READING-ORDER.md` to this new doc.
   - The spim man page (`Documentation/spim.1`) gets entries
     for the new flags.

3. **Bridge to GUI.** The same observer pattern from Phase 1
   plus a JSON-serializable AST dump (or the existing
   `-listing` output) is enough for a future GUI to scrub
   through assembly. No GUI code in this phase — just a clean
   data surface.

**Effort:** 2-3 days.

---

## Phase 5 (optional) — Static-analysis homework infrastructure

**Goal:** make AST-walking exercises feasible as homework.

### Work items

1. **A small framework** for "write a pass" homework — a stub
   pass that walks the AST and the student fills in the
   visitor. Lives in `examples/exercises/` or similar.

2. **Three sample exercises** to seed:
   - "Find unused labels"
   - "Find branches that exceed signed-16-bit range"
   - "Find `$s` registers that aren't saved before being used
     by a callee"

3. **Test infrastructure** for the exercises (student input,
   expected diagnostic output, autograder script).

**Effort:** 3-5 days. Probably skipped unless there's an actual
course using spim for assignments.

---

## Phase 6 — Comment hygiene (the final sweep)

**Goal:** strip transitional, phase-anchored language from the
codebase once the migration is complete. Replace with clean
architectural commentary aimed at a future reader who never
lived through the rewrite.

The migration leaves behind two kinds of cruft:
1. **Transitional language** in code comments — references to
   "Phase 1," "Phase 2d," "previously SDT did," "future Phase
   3 will," "for now," "tee mode," "deferred emit," etc.
   These were useful during the rewrite as anchors but read
   as archaeological noise after the dust settles.
2. **Under-explained final design** — some functions, structs,
   and files got minimal commentary because their role was
   obvious to whoever was actively touching them. After
   migration that context is lost; a fresh reader needs the
   intent spelled out.

This phase is **the last task** in the AST migration. It runs
after Phase 2f's SDT deletion and Phase 3's pseudo-op work, so
the codebase is at its final shape before commentary gets
finalized.

### Work items

1. **Sweep every file touched during phases 1–5** for these
   patterns:
   - `Phase N` / `Phase 2d` / `Phase 2e` / etc.
   - "Previously," "for now," "until Phase N," "in the future"
   - "SDT path" / "AST mode" comparisons (after SDT is gone)
   - "tee mode" — gone after Phase 2e
   - "the new ___" — relative-to-rewrite wording
   - References to the dispatch helpers as "wrappers around the
     old action helpers" — after Phase 2f they ARE the action
     surface

2. **For each function and struct member**, ensure there's a
   single concise comment that explains:
   - What the thing represents (one sentence)
   - Any non-obvious invariant or ownership rule
   - Cross-references to related types (one or two)

   The bar: a reader who's never seen spim before should be
   able to navigate `include/ast.h`, `src/emit.c`, and the
   parser within ~30 minutes.

3. **File-level header comments** for the files added during
   migration (`asm_event.h/.c`, `ast.h/.c`, `emit.c`):
   - One paragraph on the file's role
   - Pointer to the parser/emit interface
   - Note about ownership conventions for any allocated
     payloads

4. **Update the `tasks/` directory:**
   - Move completed plan docs into `tasks/archive/` or similar
     (PLAN-parse-tree-investigation.md, PLAN-parse-tree-migration.md,
     parse-actions-catalog.md, handwritten-parser-migration.md,
     handwritten-parser-design.md, scanner-parser-inventory.md).
   - Keep at the top level only the docs that describe **current
     architecture**, not migration history.

5. **`README.md` / curriculum docs**: confirm any "parser is
   hand-written recursive descent" claims are still accurate
   and updated to mention AST.

6. **`SESSION_NOTES.md` (or equivalent rolling log)**: archive
   the migration phase entries into a one-paragraph "the AST
   migration happened in May–June 2026" historical note.

### Scope guardrails

- **Do not** rewrite working code that's correctly commented
  already.
- **Do not** add comments to obvious code (assignment,
  one-line helper, named-after-its-purpose).
- **Do** prioritize: file headers > struct definitions >
  exported functions > internal helpers > inline blocks.
- **Do not** turn this into a documentation-overhaul project
  — the goal is "remove phase noise, fill in non-obvious
  intent at structural boundaries." Anything beyond that is
  a separate task.

### Verification

- Build + full regression suite still green.
- `grep -ri "phase [0-9]" src/ include/` returns nothing.
- `grep -ri "for now\|TODO.*phase\|previously" src/ include/`
  returns only legitimate uses (e.g. "for now" describing a
  current-state limitation, not a migration anchor).
- Visual code review of `include/ast.h`, `src/emit.c`,
  `src/parser.c`, `src/asm_event.{c,h}` to confirm a fresh
  reader could navigate.

**Effort:** 1 day. Mechanical; no risk.

---

## Total effort

| Phase | Days | Cumulative |
|---|---|---|
| 1: Event log | 1-2 | 1-2 |
| 2a: Action catalog | 0.5 | 2-2.5 |
| 2b: AST types | 1 | 3-3.5 |
| 2c: Constructors | 1 | 4-4.5 |
| 2d: Parser produces AST | 5-8 | 9-12.5 |
| 2e: Two-pass emit | 2-3 | 11-15.5 |
| 2f: Flip default + delete | 1-2 | 12-17.5 |
| 3: Pseudo-ops first-class | 3-5 | 15-22.5 |
| 4: Teaching surfaces | 2-3 | 17-25.5 |
| 5 (optional): Homework | 3-5 | 20-30.5 |
| 6: Comment hygiene | 1 | 21-31.5 |

Realistic estimate: **3-4 weeks** of focused work for phases 1-4 + 6.
Phase 5 only if there's a course needing it.

---

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Behavioral drift between SDT and AST paths | `-parser=ast` flag keeps both alive; listing-file from Phase 1 is the diff oracle. |
| Regression suite gaps (corner cases not covered) | List corners explicitly during Phase 2a action-catalog work; add new tests before refactor not after. |
| Pseudo-op expansion details (which temp registers, what form) drifts | Authoritative table in one place (extend the explain.c table); both old and new parsers consult it. |
| Symbol-table population order matters for some downstream consumer | Audit during Phase 2a; if any consumer (REPL, explain) depends on parse-time population, document and replicate in Phase 2e's pass 1. |
| Memory cost of the AST for large programs | Estimate ~50 bytes/node × O(10k) nodes = MB-scale. Not a real risk. Free the AST after emit if needed. |
| Loses the "spim is a one-pass assembler, easy to read" pedagogy | Compensated by gaining the "spim teaches the canonical two-pass model" pedagogy. Net positive for college audience. |

---

## Open design questions (decide before starting Phase 2d)

1. **`make_imm_expr` / `make_addr_expr` / `imm_expr` / `addr_expr`
   — are these AST or simulator types?** They predate this plan.
   Cleanest: they become AST node payloads, with the simulator
   getting a resolved-int variant after pass 1.

2. **Where does the AST live across the assembly run?** Heap,
   parented under a `module_t` or similar root. Freed after
   emit completes — unless `-print-ast` or GUI integration
   wants it persisted.

3. **Should `.set noreorder` / `.set noat` actually be honored?**
   Today they're parsed and ignored. The AST refactor is a
   natural inflection point to either honor them properly or
   document the choice.

4. **The error-recovery `sync_to_nl` mechanism** — currently
   skips the bad line and continues. In the AST world, do we
   produce a partial AST including an `AST_ERROR` node? Or
   discard the line entirely?

---

## Order of operations (recommended commit cadence)

1. Land Phase 1 (event log + listing flag + golden test). 1 PR.
2. Land Phase 2a (catalog markdown). 1 PR, doc-only.
3. Land Phase 2b+2c together (AST types + constructors).
   1 PR, pure additions, no behavior change.
4. Land Phase 2d behind `-parser=ast`. Multiple PRs probably —
   one per parser subsection (operand parsers, R-type, I-type,
   J-type, directives, etc.). Regression suite green at every PR.
5. Land Phase 2e (two-pass emit). 1 PR.
6. Run with `-parser=ast` as default for a couple weeks of
   real use. Fix anything that surfaces.
7. Land Phase 2f (flip default, keep old as fallback). 1 PR.
8. Land Phase 3 (pseudo-op first-class + `-show-expansion`).
   Probably 2-3 PRs.
9. Land Phase 4 (`-print-ast`, docs, GUI bridge). 1 PR.
10. Delete the old SDT path. 1 PR.
11. (Optional) Phase 5 homework infrastructure.

---

## Companion documents

- `tasks/PLAN-parse-tree-investigation.md` — decision rationale (why B over A or C).
- `tasks/handwritten-parser-migration.md` — previous large parser migration; reference for shape and cadence.
- `tasks/scanner-parser-inventory.md` — Phase 0 inventory of the current parser; useful for Phase 2a.
