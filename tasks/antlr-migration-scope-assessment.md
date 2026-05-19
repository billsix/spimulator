# ANTLR migration — scope and risk deep-dive

Companion to [`antlr-migration.md`](antlr-migration.md).  The
parent task asked for a Phase 0 inventory of what flex+bison
are doing today.  This document is a tighter scope-and-risk
read on top of that: how big the job actually is, and what the
specific failure modes are for the `/examples` curriculum if
the parser behaves even slightly differently after the
migration.

## TL;DR

- **Scope: large but finite.**  ~4300 lines of flex+bison
  source, 35 external entry points into spim's C runtime, one
  X-macro list of 381 keywords, 25 silently-resolved
  shift-reduce conflicts.  Mechanical translation possible;
  pseudo-op expansion semantics are the hard part.
- **Total effort: 2-3 months** of focused work split across
  five phases.  Phase 0 (inventory) is ~1 week.  Phase 3 (full
  coverage) is ~4-6 weeks.
- **Risk to `/examples`: high but mitigable.**  Every demo
  depends on exact pseudo-op expansion sequences, exact error
  message shape, and exact resolution of grammar ambiguities.
  A parity-comparison harness (run both parsers, diff results)
  is essential before flipping the default.
- **Recommendation: do Phase 0 + 1 only.**  Write the inventory
  and the grammar sketch.  Do not pilot or migrate until the
  inventory makes the cost concrete.  If after Phase 1 the
  effort estimate holds, decide at the Phase 2 gate.

## Size of the surface

### Code size

| File | Lines |
|---|---|
| `src/scanner.l` | 742 |
| `src/parser.y` | 2987 |
| `include/op.h` (X-macro keyword list) | 541 |
| **Total front-end** | **4270** |
| `tests/tt.core.s` (torture test) | 4864 |
| Other `tt.*.s` regression tests | ~6800 |
| **Total tests exercising the parser** | **~11600** |

### Grammar shape

- `parser.y` declares **390 `%token` directives** — every MIPS
  opcode, every assembler directive, plus generic tokens
  (`Y_INT`, `Y_ID`, `Y_REG`, etc.).
- **106 grammar production rules.**
- One central production, `ASM_CODE`, contains **119
  alternatives** spanning ~1050 lines (lines 592-1642).  Each
  alternative dispatches into the C runtime via a 1-to-N call
  pattern.
- `ASM_DIRECTIVE` has **46 alternatives** covering `.data`,
  `.text`, `.word`, `.asciiz`, etc.
- **25 declared shift-reduce conflicts** (`%expect 25`).  These
  are silently resolved by bison's default (shift) policy.
  Documenting what each one is and how the default resolution
  matches student expectations is part of Phase 0.

### Scanner shape

- **No lexer start conditions** (`%x` / `%s`).  Single-state
  scanner.  Simplifies the ANTLR translation considerably —
  no mode-switching to model.
- **~15 distinct lex rules**: whitespace, newline, comment,
  signed/unsigned/hex integer, FP literal, identifier+keyword
  combined, `$reg`, punctuation/operators, comma, string,
  char literal, fall-through error.
- **Custom `yywrap`** inserts a `\001` marker before hard EOF
  so `Y_EOF` is returned as a non-zero token (bison reserves
  0 for EOF).  ANTLR has a different EOF model and won't need
  this hack.
- **Custom `erroneous_line()`** (`scanner.l:554`) reconstructs
  the source line with a `^` caret marker for error messages.
  This is what the user sees on a parse error.

### X-macro coupling

`op.h` is included **6 times** across the build, with `OP()`
redefined each time to produce a different table:

| Site | What gets built |
|---|---|
| `scanner.l:606` | `keyword_tbl[]` — string → opcode-token lookup for the lexer |
| `inst.c:170` | `name_tbl[]` — sorted-by-name disassembly table |
| `inst.c:571` | `i_opcode_tbl[]` — sorted-by-encoded-opcode |
| `inst.c:904` | `a_opcode_tbl[]` — sorted-by-actual-opcode |
| `dump_ops.c` | dev tool, dumps all opcodes |

The X-macro is **already** generating grammar-adjacent data.
For ANTLR, a build-time preprocessor script can extract the
381 `OP()` entries and emit a `keywords.g4` grammar fragment;
the same script can update the C-side tables.  Single source
of truth preserved.

### External surface from parser semantic actions

The grammar's `{ }` action blocks call into **35 distinct C
functions** in the spim runtime.  Top callers by frequency:

| Function | Calls |
|---|---|
| `imm_expr` (constructor family) | 101 |
| `addr_expr` (constructor family) | 70 |
| `r_type_inst` | 67 |
| `i_type_inst` | 38 |
| `r_co_type_inst` | 15 |
| `r_sh_type_inst` | 14 |
| `nop_inst` / `make_addr_expr` / `const_imm_expr` | 10 each |
| `user_kernel_data_segment` | 8 |
| (~25 others) | misc |

These functions stay unchanged.  The ANTLR visitor methods
call exactly the same C functions with the same arguments.
This is the good news: the C runtime is a clean API.

### Postlude — the pseudo-op expansion engine

`parser.y` after `%%` (line 2685+) contains **22 helper
functions** that aren't grammar — they're the pseudo-op
expansion logic.  Examples:

- `div_inst()` — `div $rd, $rs, $rt` (a pseudo-op) emits
  `bne $rt, $0, +3 / nop / break / div $rs, $rt / mflo $rd`.
  Six instructions for one source line.
- `mult_inst()`, `set_le_inst()`, `set_ge_inst()`,
  `set_eq_inst()` — similar.

These are **C code that happens to be co-located with the
grammar**.  They can move to a separate `pseudo_op.c` file
unchanged.  Not bison-specific.

## Effort, phase by phase

| Phase | Description | Effort |
|---|---|---|
| 0 | Inventory document (pre-authorized) | ~1 week |
| 1 | Grammar design + keyword-extraction build script | ~3-5 days |
| 2 | Pilot: ANTLR parser covering R-type + I-type + `.data`/`.text` | ~2 weeks |
| 3 | Full coverage incl. pseudo-op expansion + FP family | ~4-6 weeks |
| 4 | Drop flex+bison, cleanup, docs | ~2-3 days |
| **Total** | | **~2-3 months** |

The phase boundaries are gates: the project can be killed at
any of them once cost/benefit becomes clear.

## Risk to `/examples` curriculum

The risk vector is divergent parser output: ANTLR's grammar
accepts the same source but produces different *instruction
sequences* (or different *errors*) than bison.  Every demo
the curriculum ships is sensitive to this because:

1. **Pseudo-op expansion is observable.**  Students see the
   PC advance by 6 words for one `div` source line.  `-explain`
   mode narrates each expansion step.  The expected exit code
   under `tt.unaligned.s` is `132 = 128 + ExcCode 4`; that
   ExcCode depends on which exact instruction the alignment
   fault hit.  Off-by-one in the expansion sequence is a
   regression.
2. **25 silently-resolved shift-reduce conflicts.**  Bison's
   default (shift) policy resolved them one way.  ANTLR's
   LL(*) may resolve them another way.  Most-likely-broken
   construct families:
   - `LOAD_OPS DEST ADDRESS` where `ADDRESS` is just an integer
     vs an `(reg)` form vs an `expr(reg)` form.  Bison's
     default handles "is the next token an opening paren" via
     1-token lookahead; ANTLR will figure this out automatically
     but might prefer a different alternative when both match.
   - Branch instructions with optional offsets.
3. **Error message format is now load-bearing.**  After the
   Unix-process-conformance fixes, `parse_errors_seen > 0` →
   exit 2.  The current error message format is
   `spim: (parser) syntax error on line N of file F\n<source>\n   ^`
   produced by `scanner.l:554`'s `erroneous_line()`.  If
   ANTLR's listener emits a differently-shaped message, the
   curriculum's own diagnostic UX changes too.
4. **Octal/escape handling.**  `scan_escape()` and `copy_str()`
   in `scanner.l` decode `\NNN`, `\n`, `\t`, etc.  Today's
   octal escape parser has a recently-fixed bug (`\134` →
   `\\`, fixed 2026-05-19 in scanner.l:493).  The ANTLR port
   needs to reimplement these correctly; the regression
   `tests/tt.octal_escape.s` guards against losing the fix.
5. **Label-resolution timing.**  Forward references resolved
   in a second pass via `flush_local_labels()` / `clear_labels()`
   sequencing.  Bison's action ordering is deterministic; an
   ANTLR visitor traversal needs to preserve the same order.
6. **The `\001` EOF marker.**  `scanner.l:386`'s `yywrap()`
   trick.  ANTLR's `EOF` token is real; the marker hack
   disappears, but anything in spim that depends on a
   `Y_EOF` *token* (not the parser-level EOF) needs to be
   audited.

### Mitigations

- **11600 lines of test asm.**  `tt.core.s` (4864 lines) is
  the "torture test" — runs every R-type and I-type
  instruction; should catch most regressions.  Plus the
  `/examples` demos (40+ asm files) as integration tests.
- **Parity comparison harness, mandatory before flipping the
  default.**  Build both spim-bison and spim-antlr; run every
  `tt.*.s` and every `examples/src/*/*.asm` through both;
  diff the assembled text/data segments byte-for-byte.  Same
  bytes = same behavior.  This is the Phase 2 gate.
- **Side-by-side parsers during transition.**  Build both
  parsers into the binary; switch via a `-parser=antlr` flag.
  Run CI with both for as long as it takes to gain confidence,
  then drop bison.
- **Test additions.**  Phase 2 should add a small targeted
  test (`tt.pseudo_ops.s`) that exercises every pseudo-op
  expansion individually, with expected text-segment contents
  documented as a golden file.

### Risks that aren't mitigable

- **JVM build dependency.**  ANTLR's `antlr-4.x-complete.jar`
  needs a JVM to generate code.  Anyone building spim from
  source needs JRE installed.  Fedora's `antlr4` package
  declares this dependency; the Dockerfile would grow by
  ~150 MB.  No way to dodge without abandoning ANTLR.
- **C++ runtime.**  spim is currently pure C.  The ANTLR4 C++
  runtime introduces a `libstdc++` dependency.  Probably
  already pulled in via libedit on Fedora, but worth verifying
  on stripped-down builds.
- **Maintainer comfort.**  ANTLR grammar syntax is its own
  thing.  Bill currently maintains flex+bison; switching
  means re-learning the new tool.  This is paid back over
  time but real up-front.

## Where the migration helps

Five concrete pains the migration would relieve:

1. **Adding a new instruction is 4 edits today** (op.h,
   scanner.l, parser.y, inst.c per-tabletype) — could become
   1 edit (op.h alone, with grammar regenerated from it).
2. **Error messages are limited to "syntax error"** at most
   error positions.  ANTLR's diagnostic listener can produce
   "expected REG or INT, got Y_NL" automatically.  Pedagogical
   win.
3. **No IDE / grammar tooling for flex+bison.**  ANTLR has
   IDE plugins (IntelliJ, VS Code), grammar visualizers,
   ATN diagrams.  Useful when teaching the parser itself, or
   when extending the grammar.
4. **25 shift-reduce conflicts are technical debt.**  ANTLR's
   LL(*) typically eliminates them or surfaces them as
   semantic predicates with explicit handling.
5. **Grammar can grow.**  Today every new opcode is a token,
   a name table entry, a grammar production alternative, and a
   semantic action.  Most of those become generated.

## Where the migration doesn't help

- **The hard part isn't the grammar.**  It's the pseudo-op
  expansion logic (22 helper functions, ~600 lines of C in
  the postlude).  That code is C-on-spim-internals; it
  doesn't get easier or harder under ANTLR.
- **The teaching surface is the same after the migration.**
  Students still see `lui`/`ori` expansions for `la`, still
  see PC layouts, still hit the same exception codes.  The
  curriculum doesn't get better; it just doesn't get *worse*.
- **No runtime perf win.**  ANTLR4 is typically slower than
  bison on simple grammars.  For spim's typical input
  (1000-line asm file), parse time is imperceptible either
  way; not a motivation.

## Recommendation

Do Phase 0 (inventory) and Phase 1 (grammar design +
keyword-extraction script).  These together produce a
**concrete-numbers document** that turns the "2-3 months"
estimate into "X grammar rules, Y predicate insertions, Z
postlude functions to relocate" — quantified enough to decide
at the Phase 2 gate whether to actually pilot.

**Do not start Phase 2 (pilot) without the parity-comparison
harness in place first.**  The curriculum's risk vector is
divergence between bison and ANTLR output; the only way to
detect that early is byte-for-byte comparison on the existing
test corpus.

If after Phase 1 the estimate doubles (4-6 months), or if the
parity harness suggests pseudo-op expansion semantics are
genuinely hard to match, that's a strong signal to stop.

If Phase 1 reveals it's actually closer to 1-2 months and the
parity harness shows bison and ANTLR agree on the core
grammar, that's a strong signal to proceed.

## Open questions for Bill

- Is the **pedagogical value of "add a new instruction in one
  place"** worth a JVM build dependency for the simulator?
- Is **error-message quality** a real student pain today, or
  does the post-Unix-conformance setup (parse_errors_seen,
  exit-2 propagation) already cover the practical UX?
- Is anyone other than you maintaining the parser?  If you're
  the sole maintainer, the "more maintainable" argument is
  more about your personal experience editing the grammar
  than about a broader benefit.
- Is there a **non-ANTLR alternative** worth considering?
  Specifically: a hand-written recursive-descent parser (like
  what most modern compilers ship — gcc, clang, rustc all
  hand-write).  Effort similar to ANTLR migration, no JVM
  dependency, full control over error messages.  Smaller
  ecosystem tooling but maybe enough for one maintainer's
  one project.
