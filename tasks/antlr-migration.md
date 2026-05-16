# Task: investigate migrating flex+bison to ANTLR4 (C++)

## Goal

Eventually replace the existing flex (`src/scanner.l`) and bison
(`src/parser.y`) front-end with an ANTLR4 grammar driven by ANTLR4's
C++ runtime. Same input syntax accepted, same semantic effects on
the simulator's data structures, but a more modern, maintainable
parsing framework with better error recovery, IDE tooling, and
grammar-debugging support.

**This task is intentionally study-first.** The user is aware the
migration itself is a large undertaking. The first concrete
deliverable is *not* a working ANTLR grammar — it's a complete
inventory of what flex and bison are doing today, written down so a
go/no-go decision on the rewrite can be made with full information.

## Phase 0 — study (the only thing to do right now)

Produce a document — call it `tasks/scanner-parser-inventory.md` —
that catalogs:

### From `src/scanner.l`

1. **Lexer states** (`%x`, `%s`) — does the scanner switch between
   start conditions for string literals, comments, etc.?
2. **Token list.** Every `Y_*` returned by the lexer. Cross-reference
   against `include/op.h` (which uses the same Y_* names via the
   X-macro). Note which tokens are produced from regex matches vs.
   from the `OP()` X-macro keyword table.
3. **Semantic actions in `{ }` blocks.** Each rule's C action — what
   gets stored in `yylval`, what side effects on global state.
4. **Custom entry points** — `push_scanner` / `pop_scanner` /
   `yywrap` / `initialize_scanner`. The custom `yywrap` inserts a
   `\001` marker to terminate input cleanly; that's a quirk ANTLR
   will need an equivalent of.
5. **Global state shared with the parser:** `yylval`, `yytext`,
   `line_no`, `current_line`, `line_returned`, `eof_returned`.
6. **The keyword-table generation pattern.** `scanner.l` line ~606
   includes `op.h` to populate a hash table mapping keyword strings
   to Y_* tokens. ANTLR doesn't natively support runtime token
   tables — research how to reproduce this. Likely options:
   - Generate the grammar's terminal token alternation from
     `op.h` at build time (preprocessing step).
   - Or do "all identifiers tokenize as IDENT, classify in a
     semantic predicate at parse time."

### From `src/parser.y`

1. **Grammar productions.** Every `:` rule. Document the high-level
   shape: probably a flat list of `instruction | directive | label`
   productions, with a long alternation under `instruction` for
   each opcode family.
2. **Semantic actions.** Each rule's `{ }` action — what AST or
   side-effect happens. spim doesn't have a separate AST: most
   actions directly populate `instruction` records via calls into
   `inst.c` / `data.c`. That's important for the migration —
   ANTLR's visitor-pattern approach is a closer fit than its
   listener pattern for this style.
3. **Conflicts.** Any shift-reduce or reduce-reduce conflicts
   bison reports. These need explicit handling in ANTLR (often
   via lookahead `(*)` or semantic predicates).
4. **Error recovery.** Does parser.y use `error` productions?
   What's the recovery strategy? ANTLR's recovery model is
   different (sync sets); we need to match the existing UX.
5. **External calls.** Functions parser.y calls into:
   `make_label_global`, `record_label`, `r_type_inst`,
   `i_type_inst`, etc. These stay as-is; the new parser just
   needs to call them with the same arguments.

### From `meson.build`

1. The `custom_target('lex', ...)` and `custom_target('parser', ...)`
   rules — how generated files feed into the build. ANTLR uses a
   similar generate-at-build-time model but via a Java jar
   (`antlr-4.x-complete.jar`). Document the existing flow so the
   ANTLR equivalent is clear.

### Output of Phase 0

A self-contained document (`tasks/scanner-parser-inventory.md`)
that someone with no spim background can read and answer:
"What does the current parsing pipeline accept, what does it do
with each piece, and what shared state ties it to the rest of the
simulator?"

That document is the input to a Phase 1 design decision (proceed
with ANTLR migration, or not).

## Phase 1 — design (only if Phase 0 indicates the migration is
            worth doing)

- Sketch the ANTLR grammar. Likely structure:
  ```
  program: line* EOF ;
  line: directive | instruction | label_def | NEWLINE ;
  directive: '.data' | '.text' | '.globl' IDENT | '.word' int_list | ... ;
  instruction: opcode operand_list ;
  ...
  ```
- Decide how keywords are produced. Generating the grammar from
  `op.h` at build time is the most maintainable; document the
  build-step.
- Decide on visitor vs. listener pattern. (Visitor — spim's actions
  are mostly "given this operand list, build an instruction" which
  is naturally a return-value walk.)
- Plan the bridge from C++ visitor methods back to spim's C
  functions (`r_type_inst`, etc.). C++-to-C is trivial (extern "C"
  declarations); the real question is whether the visitor passes
  spim's data structures around correctly.

## Phase 2 — pilot

Build a working ANTLR grammar that accepts a *subset* of the input
syntax (e.g. just the integer R-type and I-type instructions plus
`.data` / `.text` directives) and parses one of the regression
tests into the same internal data structures as bison would.

Compare: run both parsers on the same input, dump the resulting
text/data segments, diff. They should be identical.

## Phase 3 — full coverage

Expand the grammar to cover every production in `parser.y`. Replace
the bison-driven path with the ANTLR-driven path. Build with both
side-by-side until parity is confirmed, then drop bison.

## Phase 4 — drop flex+bison

Remove `src/scanner.l`, `src/parser.y`, the `lex_gen` /
`parser_gen` custom targets in meson.build. Add ANTLR runtime
dependency, ANTLR generate-at-build-time custom target.

## Risks / open questions

- **License compatibility.** ANTLR4 C++ runtime is BSD-3-Clause —
  fully compatible with spim's BSD license. No issue.
- **Build dependency.** ANTLR4 needs a JVM to generate code from the
  grammar at build time, plus the runtime library at link time.
  Fedora packages both (`antlr4` package + `antlr4-cpp-runtime`).
  The Dockerfile would grow by those two packages. Build hosts
  without a JVM would need one — acceptable trade-off.
- **Performance.** ANTLR4 parsers are typically slower than bison
  for normal-size inputs but not by orders of magnitude. For an
  assembly file of thousands of lines, the difference is
  imperceptible to a student.
- **Error messages.** ANTLR's default error reporting is more
  helpful than bison's "syntax error" but differently shaped.
  Worth deciding whether to keep spim's existing error message
  format (would require custom error listeners) or accept ANTLR's
  defaults.
- **C++ vs. C boundary.** spim is otherwise pure C. Linking an ANTLR
  C++ visitor in means the simulator binary depends on `libstdc++`.
  Probably already pulled in via libedit on Fedora, but worth
  checking on stripped-down builds.
- **The X-macro trick.** Today `op.h` is included in multiple
  translation units to build different per-TU tables. ANTLR's
  grammar generation happens once at build time, so the "include
  this list and #define OP to whatever" trick doesn't apply to the
  parser side. The keyword list still needs to be propagated
  somehow — likely by generating an ANTLR grammar fragment from
  `op.h` via a small build-time script. Document this in Phase 0.

## Effort

- Phase 0 (study + write inventory): ~1 week of careful reading.
  This is the only phase the user has greenlit.
- Phase 1 (design): ~3-5 days.
- Phase 2 (pilot): ~2 weeks.
- Phase 3 (full coverage): ~4-6 weeks.
- Phase 4 (cleanup): ~2-3 days.

Total if all phases proceed: 2-3 months. The point of the
phased approach is that the project can be killed at any boundary
once the cost/benefit becomes clear.

## First concrete deliverable

Start `tasks/scanner-parser-inventory.md`. Read every line of
`src/scanner.l` and `src/parser.y` and document what's there. Don't
write any ANTLR yet. The inventory document is the gate that lets
the rest of the project be evaluated honestly.
