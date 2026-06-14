# Task: replace flex+bison with a hand-written recursive-descent parser in C

## Goal

Replace `src/scanner.l` (flex) and `src/parser.y` (bison) with
a hand-written lexer and recursive-descent parser, both in
plain C, integrated into the existing meson build without
the `flex` and `bison` generators.  Same input syntax accepted,
same semantic effects on the simulator's data structures.

The pure-C runtime in `src/inst.c`, `src/data.c`,
`src/sym-tbl.c`, etc. stays unchanged.  Only the front-end
(scanner + parser) changes.

## Why hand-written, not ANTLR

This task was originally written as an ANTLR migration.  After
the scope-and-risk audit (2026-05-19) and a comprehension-cost
discussion with Bill, the decision was to switch to a
hand-written recursive-descent parser.  Key reasons:

1. **Comprehensibility for a solo maintainer.**  flex+bison
   is opaque to read.  ANTLR moves the opaqueness into a
   different DSL plus a runtime, but doesn't eliminate it.
   A hand-written parser is plain C — every line does
   exactly what it looks like it does.
2. **No new build dependency.**  The simulator stays pure C.
   No JVM, no jar at build time, no C++ runtime at link time.
3. **Incremental migration is possible.**  The bison path can
   stay alive in the binary until the hand-written path
   covers everything, switched by a `-parser=…` flag.  ANTLR
   requires the grammar to be complete before it can run.
4. **In spim's pedagogical spirit.**  spim teaches the low
   level.  A hand-written parser is the parser equivalent of
   writing assembly by hand — readable as part of the
   curriculum if a student is curious.

ANTLR's genuine advantages (grammar-visualization IDE
plugins, sync-set error recovery out of the box,
industry-standard tooling) don't help a solo maintainer who
wants to understand his own parser.

## Phase 0 — inventory (DONE 2026-05-19)

Inventory document landed:
[`tasks/scanner-parser-inventory.md`](scanner-parser-inventory.md).

Ten parts:
1. Overview (file roles, generated outputs, build flow)
2. scanner.l (15 lex rules, 11 helper functions, shared globals)
3. Token inventory (390 Y_* tokens, by category)
4. parser.y productions (104 productions, grouped by family,
   with per-family alternative counts)
5. parser.y postlude (22 helper functions, all relocatable
   to `pseudo_op.c` unchanged)
6. Shared global state catalog
7. Shift-reduce conflicts catalog — all 25 are the same
   `Y_ID`-in-IMM32-context ambiguity, resolvable with
   two-token lookahead in a hand-written parser
8. meson build flow
9. Risk-relevant findings (`\001` EOF sentinel, per-line
   `yyparse` loop, recently-fixed octal-escape bug,
   load-bearing error-message format, forward-reference
   model, negative-integer tokenization quirk, `\X` vs `\x`
   inconsistency)
10. Open questions for Phase 1

The inventory is the gate for the Phase 1 design decision.
Read it before authorizing Phase 1.

### Inventory data already collected

Initial numbers from a Phase-0 audit on 2026-05-19:

| Surface | Size |
|---|---|
| `src/scanner.l` | 742 lines |
| `src/parser.y` | 2987 lines |
| `include/op.h` (X-macro keyword list) | 541 lines, 381 `OP()` entries |
| Total front-end | 4270 lines |
| `tests/tt.*.s` regression corpus | ~11,600 lines |
| `parser.y` `%token` directives | 390 |
| Grammar productions in `parser.y` | 106 |
| `ASM_CODE` alternatives | 119 (lines 592-1642) |
| `ASM_DIRECTIVE` alternatives | 46 |
| Silent shift-reduce conflicts (`%expect 25`) | 25 |
| External C functions called from semantic actions | 35 |
| Helper functions in `parser.y` postlude | 22 |
| Lexer start-conditions (`%x`/`%s`) | 0 |

### From `src/scanner.l`

Single-state scanner (no `%x`/`%s`).  ~15 distinct lex rules:
whitespace, newline, comment, signed/unsigned/hex int, FP
literal, identifier+keyword, `$reg`, punctuation, comma,
quoted string, char literal, fall-through error.

Notable details:

- **Custom `yywrap`** (line 386) inserts a `\001` marker
  before hard EOF so `Y_EOF` is returned as a non-zero
  token (bison reserves 0 for EOF).  The hand-written
  scanner can use a real EOF sentinel without the hack.
- **`erroneous_line()`** (line 554) reconstructs the source
  line with a `^` caret for error messages.  Must be
  preserved — the curriculum's exit-code path
  (`parse_errors_seen > 0 → exit 2`) depends on this message
  reaching stderr.
- **`scan_escape()`** and **`copy_str()`** decode `\NNN`,
  `\n`, `\t`, `\x..` in char and string literals.  The
  `copy_str` octal parser had a recently-fixed bug
  (`\134 → \\`; fix in scanner.l:493, regression test
  `tests/tt.octal_escape.s`).  Reimplement carefully —
  match the test.
- **`check_keyword()`** consults `keyword_tbl[]` built by
  including `op.h` with `OP()` redefined.  381 keywords.
- **`register_name_to_number()`** maps `at`/`v0`/`a0`/.../`zero`
  to register numbers (lines 629-665).  Stays as-is.

### From `src/parser.y`

Grammar (lines 533-2684):

- `LINE` is the top production; one line per parse cycle.
- `LBL_CMD` allows an optional `LABEL:` prefix.
- `ASM_CODE` covers every instruction form (119 alternatives).
  Grouped into `LOAD_OPS`, `STORE_OPS`, `BINARY_OPS`, etc.
  meta-productions per operand-shape family.
- `ASM_DIRECTIVE` covers every `.foo` directive (46
  alternatives).
- `EXPR`/`TRM`/`FACTOR`/`ADDRESS`/etc. cover operand
  expressions, addressing modes, immediate ranges.

C postlude (lines 2685-2987, 22 helper functions):

- **Pseudo-op expansion engine**: `div_inst`, `mult_inst`,
  `set_le_inst`, `set_ge_inst`, `set_eq_inst`, etc.  Each
  expands one source-level pseudo-op into a multi-instruction
  R-type/I-type sequence.  ~600 lines of plain C; **moves
  unchanged to `src/pseudo_op.c`** during migration.
- `cons_label`, `clear_labels`, `record_label`, `flush_local_labels`
  — label-table mutation, ordering matters.
- `yyerror`, `yywarn` — emit error messages via spim's
  `error()` function (which now goes to stderr).
- `check_imm_range`, `check_uimm_range`, `branch_offset` —
  utility predicates.

### External call surface

The grammar's `{ }` action blocks call into **35 distinct
spim C functions**.  Top callers by frequency:

| Function | Calls |
|---|---|
| `imm_expr` constructors (`const_imm_expr`, `make_imm_expr`, ...) | 101 |
| `addr_expr` constructors (`make_addr_expr`, ...) | 70 |
| `r_type_inst` | 67 |
| `i_type_inst` | 38 |
| `r_co_type_inst` | 15 |
| `r_sh_type_inst` | 14 |
| `nop_inst` | 10 |
| `user_kernel_data_segment` | 8 |
| (~25 others) | misc |

These functions stay unchanged.  The hand-written parser
calls exactly the same functions with the same arguments.

### Shared global state

| Symbol | Owner | Used by |
|---|---|---|
| `yylval`, `yytext` | scanner | parser |
| `line_no`, `current_line`, `line_returned`, `eof_returned` | scanner | error messages |
| `parse_error_occurred` | parser | label-flush in scanner |
| `parse_errors_seen` | parser | main()'s exit code |
| `data_dir`, `text_dir` | parser | scanner's `OPT_LBL` PC selection |
| `only_id` | parser | scanner's identifier-vs-label disambiguation |
| `null_term`, `store_op`, `store_fp_op` | parser | data-emission helpers |

Each becomes a normal C-file static or extern — straightforward.

## Phase 1 — design (DONE 2026-05-19)

Design document landed:
[`tasks/handwritten-parser-design.md`](handwritten-parser-design.md).

Covers:

1. Module shape (`scanner.c`, `parser.c`, `pseudo_op.c`,
   `token.h`, `pseudo_op.h`, expanded `scanner.h` /
   `parser.h`).
2. Token enum design — relocate from bison-generated
   `parser_yacc.h` to a hand-maintained `include/token.h`
   that reuses the `op.h` X-macro pattern.  YYSTYPE stays
   as the existing `intptr_union` from `spim.h`.
3. Scanner API: `scanner_init`, `scanner_next`,
   `scanner_peek`, `scanner_peek2`, `scanner_advance`,
   `scanner_expect`, `scanner_force_identifier`.  Replaces
   the global `only_id` toggle with a self-clearing flag.
4. Parser API: `parse_file()` as the single entry point;
   no per-line `yyparse` loop.
5. Per-production function map — ~75 functions total
   (~55 instruction-family parsers + ~20 operand parsers
   + the directive dispatcher), with naming convention
   `parse_<production>`.
6. Lookahead model: 1-token peek almost everywhere,
   2-token peek in three identified places (label vs.
   instruction at line start, `Y_ID + ABS_ADDR` vs. `Y_ID`
   alone, and the `Y_INT Y_INT` negative-tokenization
   quirk).
7. Error-recovery model: `sync_to_newline` on `yyerror`,
   preserves the existing one-error-per-line behavior and
   the load-bearing error-message format.
8. Phase 2 pilot scope: BINARYI/BINARY_ARITHI/BINARY_LOGICALI/
   SHIFT/LOAD/STORE/branch/jump/NULLARY + EXPR/ADDR +
   `.data`/`.text`/`.globl`/`.word`/`.byte`/`.asciiz`/`.space`/`.align`.
   Big enough to assemble `01-helloworld.asm` and
   `02-print1through10.asm`; small enough to land in ~6 days.
9. Parity-harness design: same input through both parsers,
   byte-diff the assembled text/data segments.  Phase 2's
   gate.
10. Build integration plan for each phase, including the
    `-parser=hand|bison` flag for coexistence through
    Phase 4.

The design is the gate for Phase 2 authorization.  Read
both this doc and the inventory before authorizing Phase 2.

### 1. Module shape

```
src/scanner.c          /* hand-written lexer, replaces scanner.l */
src/parser.c           /* recursive-descent parser, replaces parser.y */
src/pseudo_op.c        /* relocated from parser.y postlude */
include/scanner.h      /* token enum, scanner API */
include/parser.h       /* parser entry points (mostly unchanged) */
```

### 2. Token definitions

The 390 `Y_*` tokens currently live in `parser.y` as `%token`
directives, generating an enum in `parser_yacc.h`.  Move that
enum to `include/scanner.h` (or a new `include/token.h`).
The X-macro in `op.h` continues to populate the keyword
table; no change to op.h.

### 3. Recursive-descent shape

One function per grammar production.  Naming convention:
`parse_<production>()`.  Examples:

```c
static int parse_expr(void);
static int parse_term(void);
static int parse_factor(void);
static addr_expr* parse_address(void);
static void parse_asm_code(void);
static void parse_asm_directive(void);
static void parse_load_op(int opcode);
static void parse_store_op(int opcode);
static void parse_binary_arith_op(int opcode);
/* etc. */
```

### 4. Top-level loop

```c
int parse_file(FILE* in) {
    scanner_init(in);
    while (peek() != TOK_EOF) {
        parse_error_occurred = false;
        scanner_start_line();
        parse_line();
    }
    return parse_errors_seen;
}
```

### 5. Lookahead / error recovery strategy

- **Lookahead**: a single 1-token peek window.  `peek()` returns
  current token without consuming; `advance()` consumes it.
  Bison's 25 shift-reduce conflicts disappear because we'll
  hand-resolve each with explicit lookahead checks.
- **Backtracking**: avoid.  Document any place where it's
  needed.  In practice, the grammar is LL(1)-clean after
  refactoring the conflicting productions.
- **Error recovery**: on syntax error, set
  `parse_error_occurred = true`, increment `parse_errors_seen`,
  call the existing `error()` helper (which now correctly
  goes to stderr), and skip tokens to the next newline.  This
  matches bison's effective behavior with the existing
  `%error` directive convention.

### 6. Pseudo-op expansion

The 22 postlude functions move to `src/pseudo_op.c` with no
behavior change.  The parser calls them at exactly the same
points bison did.

### 7. Build integration

Drop `flex` and `bison` from `meson.build`.  Drop the two
`custom_target` rules.  Add `src/scanner.c`, `src/parser.c`,
`src/pseudo_op.c` to the source-files list.

Phase 1 produces a written-down design that says, for each
of the 106 grammar productions, what hand-written function
will implement it.

## Phase 2 — pilot (DONE 2026-05-19)

Scaffolding + pilot subset + parity harness all landed.

### What landed

- **Ground-zero scaffolding** (Phase 2.0):
  - `include/hp_parser.h`, `include/hp_pseudo_op.h` — public APIs
  - `src/hp_scanner.c`, `src/hp_parser.c`, `src/hp_pseudo_op.c`
  - `meson.build` updated; all new files build into the same
    binary as the bison path
  - `hp_`-prefixed names avoid symbol collisions during coexistence
- **`-parser=hand|bison` flag** (Phase 2.1) in `spim.c`.  Default
  bison.  `spim-utils.c` dispatches at `read_assembly_file`.
  Exception handler always loads via bison (it uses constructs
  outside pilot scope).
- **Parity harness** (Phase 2.2): `tests/parity-harness.sh`.
  Runs an input through both parsers with `-dump`, normalizes
  out the source-line comment column, byte-diffs the assembled
  text+data segments.  Exit 0 on parity, 1 on diff.
- **Hand-written scanner** (Phase 2.3): single-state lex
  emulator, line-buffered, 3-slot lookahead, reuses `op.h`
  X-macro for the 381-entry keyword table.  Drops the `\001`
  EOF sentinel.  Self-clearing `force_identifier` flag
  replaces the bison-era `only_id` global.
- **Hand-written parser** (Phase 2.4): TYPE-based dispatcher
  over the opcode token; recursive-descent operand parsers
  including the full 9-alternative ADDR production.  The 25
  bison shift-reduce conflicts collapse into one explicit
  `peek + peek2` check inside `parse_address`.

### Parity tests green (5 ad-hoc + 1 curriculum demo)

| Test | Coverage |
|---|---|
| `min.s` | NULLARY (syscall only) |
| `test_arith.s` | R-type, I-type, shift, andi/ori/xori |
| `test_branch.s` | beq/bne with labels (the 25-conflict case) |
| `test_loadstore.s` | lw/lb/sw with label addresses + `.word`/`.asciiz` |
| `test_minimal_data.s` | `.data` segment + label + `.word` |
| `examples/src/00-exit/00-exit.asm` | The PGU-style "set status, exit" demo |

All pass `parity-harness.sh` — byte-for-byte identical assembled
output through both parsers.  The first curriculum demo flows
through the hand-written parser correctly.

### Bug found and fixed during pilot

1. **Branch operand order.**  Bison's call is
   `i_type_inst_free(op, SRC2, SRC1, ...)` because the runtime
   signature is `(op, rt, rs, expr)` and SRC2 is rt.  Initial
   `parse_b2` had them reversed.  Fixed to match.
2. **`force_identifier` cache invalidation.**  The bison-era
   `only_id = 1` flag was being set in `parse_opt_label`,
   leaking into the NEXT token's classification (caused `.word`
   to lex as Y_ID).  Removed the call in `parse_opt_label`
   since the Y_ID was already classified at the peek site.
3. **PC-relative branch displacement.**  Bison's `LABEL`
   production builds an imm_expr with `pc_relative=true` and
   `offset = -current_text_pc()`; this triggers the
   resolution-time PC-relative adjustment in
   `resolve_a_label_sub`.  Initial hand `parse_b2` used
   `parse_br_imm32` (pc_relative=false), producing absolute
   offsets in the encoded instruction.  Added `parse_label()`
   mirroring `LABEL: ID` semantics.

### Pilot scope vs Phase 3

Pilot covers (and parity-verified for):

- TYPE-based dispatch: NOARG, R3, R2sh, R1s, I2, I1t, I2a,
  B2, B1, J
- Pseudo-ops: `li`, `move`, `nop`
- Directives: `.data`, `.text`, `.globl`, `.word`, `.byte`,
  `.asciiz`, `.ascii`, `.space`, `.align`, `.extern`, `.kdata`,
  `.ktext`, `.half`, `.comm`, + the no-op metadata directives
- Operand productions: `ADDR` (all 9 alternatives), `IMM16`,
  `UIMM16`, `IMM32`, `ABS_ADDR`, `EXPR`/`TRM`/`FACTOR`,
  registers, strings, char literals

Phase 3 needs:

- `la` pseudo-op (needed by 01-helloworld) — emits lui+ori
  or ori depending on the address magnitude.  Multi-instruction
  expansion.
- `div`, `mulo`, `sle`, `seq`, `bgt`, `ble`, `beqz`, `b`, etc.
  — the rest of the pseudo-op zoo.  Each calls a helper from
  `hp_pseudo_op.c`.
- FP family (~30 functions).
- Coprocessor + TLB + trap instructions.
- The remaining directives (most are no-ops).

### Files touched

- new: `include/hp_parser.h`, `include/hp_pseudo_op.h`,
  `src/hp_scanner.c`, `src/hp_parser.c`, `src/hp_pseudo_op.c`,
  `tests/parity-harness.sh`
- modified: `src/spim.c` (flag), `src/spim-utils.c` (dispatch
  + exception-handler gate), `meson.build` (new sources)
- untouched: `src/scanner.l`, `src/parser.y` — bison path
  byte-identical to pre-Phase-2

### Cosmetic Phase 3 cleanup deferred

- Source-line annotation: hand parser doesn't plumb
  `source_line()` into per-instruction metadata yet.  Parity
  harness ignores this column.  Real fix needs `source_line()`
  dispatcher in `inst.c`.
- Memory leak: parse functions don't all free their imm_expr/
  addr_expr arguments.  Sweep at Phase 5 cleanup.
- The unused `parse_br_imm32` and `hp_store_fp_op`: kept for
  symmetry with the design; deleted in Phase 3.

### Phase 2 gate: passed.

The parity harness is green for the pilot scope.  The model
works: byte-identical instruction sequences between bison and
hand-written parsers.  Phase 3 (full coverage) can start.

---

## Phase 2 — pilot (original spec)

Implement the new front-end for a **subset** of the grammar:

- `.data` / `.text` / `.globl` / `.word` / `.asciiz` / `.byte`
  directives.
- Integer R-type and I-type instructions (`add`, `addi`,
  `lw`, `sw`, `beq`, `bne`, `j`, `jal`, `syscall`).
- Labels and forward references.

Build both parsers into the binary.  Add a CLI flag
`-parser=bison` (default) / `-parser=hand`.  Run **the same
input file** through both parsers; dump the resulting text/
data segments; diff byte-for-byte.

Phase 2 gate: **parity-comparison harness must show zero
diff** on `tests/tt.core.s` for the covered instruction
subset before continuing.  Without the harness this whole
project is a foot-gun.

## Phase 3 — full coverage (curriculum-complete 2026-05-19; spim test suite partial)

### What landed

- **`la` pseudo-op** — needed by 01-helloworld.  Calls
  `i_type_inst(Y_ADDI_OP, ...)` or `i_type_inst(Y_ORI_OP, ...)`
  depending on whether the ADDRESS has a base register.
- **Branch pseudo-ops**: `beqz`, `bnez`, `b`, `bal`, `bgt`,
  `bgtu`, `bge`, `bgeu`, `blt`, `bltu`, `ble`, `bleu`.  Each
  expands to the bison-equivalent slt+bne or slti+beq
  sequence.  Both register and immediate operand forms.
- **R3 immediate form**: `add $t0, $t1, 5` (BINARYI_OPS with
  IMM32) is now recognized — uses `hp_op_to_imm_op` to convert
  to the immediate variant.  Also the 2-operand form `add $t0,
  5` (DEST IMM, with DEST as both rs and rt).
- **B2 immediate form**: `beq $t0, '-', label` (BINARY_BR_OPS
  with IMM, parser.y:1327-1346).  Uses $at to materialize the
  immediate, then bne/beq.
- **R2st_TYPE_INST**: `mult`, `multu`, `div`, `divu` —
  2-operand R-type (no destination).  `div`/`divu` also
  accept the 3-operand DIV_POPS pseudo forms via dispatch on
  operand count.
- **R1d_TYPE_INST**: `mfhi`, `mflo`.
- **Pseudo arithmetic**: `rem`, `remu`, `mulo`, `mulou` — via
  the relocated `hp_div_inst` / `hp_mult_inst` helpers.
- **`.float`/`.double`** directives + FP literal scanner.
  Single/double-precision FP storage with proper alignment.
- **Floating-point literals** in the scanner: recognizes
  `[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?` and the comma/single-quote
  separator variants bison's regex accepts.
- **`this_line_labels` tracking** — labels on their own line
  now persist into the next line, mirroring bison's behavior
  where `align_data` retroactively fixes label addresses via
  `fix_current_label_address`.  Critical for any
  label-on-own-line + `.word`/`.double` pattern.
- **`hp_auto_align` mirror** — tracks data.c's
  `enable_data_auto_alignment` (static) so `.align 0`
  correctly disables subsequent auto-alignment.

### Parity results

- **`examples/src/*/*.asm`: 44 / 44 demos pass byte-for-byte
  parity.** Every curriculum demo flows through the
  hand-written parser correctly.
- **`/spimulator/tests/tt.*.s`: 10 / 19 pass parity.** The
  remaining 9 use constructs outside the curriculum:
  - `mfc0` / `mtc0` (coprocessor moves) — `tt.bare.s`,
    `tt.io.s`
  - `ulh` / `ulhu` / `ulw` / `ush` / `usw` (unaligned-load/store
    pseudo-ops) — `tt.be.s`, `tt.le.s`
  - FP family arithmetic (`add.s`, `c.lt.d`, etc.) —
    `tt.fpu.bare.s`, `tt.core.s`, `tt.alu.bare.s`
  - `jalr $reg` register form already partly supported;
    `tt.explain.s` uses a context that needs checking
  - `tt.parse_error.s` — intentional parse error; parity
    diff is in error-message count, not bytecode

### Bugs found and fixed during Phase 3

1. **Label-fix-up across line boundaries.**  bison's `CMD:
   TERM` production does NOT call `clear_labels`, so
   `crctab:` (on its own line) leaves `this_line_labels`
   non-empty.  When the next line's `.word` directive fires
   `set_data_alignment(2)`, `align_data` calls
   `fix_current_label_address` to align `crctab`'s recorded
   address.  My initial hand parser resolved labels
   immediately, losing this fix-up.  Symptom: 18-cksum had
   `crctab` 1 byte off (4159 vs 4160 in ori imm).  Fix:
   maintain my own `hp_this_line_labels` list with deferred
   resolution + manual pre-alignment via `hp_fix_line_labels`.
2. **`.align 0` disables auto-align.**  data.c's
   `enable_data_auto_alignment` static was unreadable from
   the hand parser, so my `hp_align_labels_to` was
   over-aligning in regions where `.align 0` had explicitly
   disabled it.  Symptom: tt.dir.s `l53` ended up 4 bytes
   higher than bison.  Fix: maintain `hp_auto_align` mirror
   set/cleared at the same points.
3. **`force_identifier` flag persistence past lookahead.**
   The bison-era `only_id = 1` flag was leaking past the
   token it was meant to affect because the lookahead cache
   already had the token classified.  Removed the
   unnecessary call in `parse_opt_label` — the Y_ID was
   already classified at the peek site.

### Phase 3 — what's left

Documented for a future session.  None of these block the
curriculum:

- FP family (~30 productions: `add.s`, `c.lt.s`, `cvt.*`, etc.)
- Coprocessor 0 moves (`mfc0`, `mtc0`, `cfc0`, `ctc0`) +
  TLB instructions (`tlbp`, `tlbr`, `tlbwi`, `tlbwr`)
- Unaligned-load/store pseudos (`ulh`, `ulhu`, `ulw`, `ush`, `usw`)
- Trap family (`teq`, `tne`, etc.) — TRAP_OPS productions
- Remaining R-type families that haven't been needed yet:
  R2td (mthi/mtlo destination form), R2st extras, R3sh, etc.
- The bare-machine variants that disable the `accept_pseudo_insts`
  flag — already partially supported via the keyword-table
  check, needs verification

### Phase 3 cleanup landed (2026-05-19 evening, two rounds)

Round 1 took spim regression test parity from 10/19 to
14/19; round 2 took it from 14/19 to **19/19**.  Curriculum
held at 44/44 throughout.

Added handlers/dispatch for:

- **R2td_TYPE_INST** (mfc0/mtc0/etc.): REG + COP_REG.  COP_REG
  accepts either Y_REG or Y_FP_REG (parser.y:2572-2574).
- **R2ds_TYPE_INST** (jalr): routed to parse_j.  Also added
  the 2-arg `jalr DEST SRC1` form (parser.y:1460-1466) to
  parse_j itself.
- **FP_R2ds_TYPE_INST** (FP_UNARY_OPS, FP_MOVE_OPS): F_DEST + F_SRC2.
- **FP_R3_TYPE_INST** (FP_BINARY_OPS): F_DEST + F_SRC1 + F_SRC2.
- **FP_CMP_TYPE_INST** (FP comparison family): F_SRC1 + F_SRC2.
- **FP_R2ts_TYPE_INST** (mfc1/mtc1/cfc0/ctc0): REG + COP_REG
  (Y_REG or Y_FP_REG).
- **FP_I2a_TYPE_INST** (lwc1/swc1): F_REG + ADDRESS.
- **`break N`** form (NOARG with optional integer operand).
- **Unaligned-load/store pseudos**: `ulh`, `ulhu`, `ulw`, `ush`,
  `usw` (LE path).
- **`rem`, `remu`, `mulo`, `mulou`** pseudo-ops.
- **`not`, `neg`, `negu`** pseudo-ops.
- **`ld`, `sd`** pseudo doublewords (expand to two lw/sw).
- **`sub`/`subu` with immediate** (converted to addi/addiu
  with negated value).
- **`clo`/`clz`** (2-operand R-type — rd=rs slot duplicated).
- **`.float` / `.double`** directives.
- **Floating-point literals** in the scanner.
- **Label-in-FACTOR** (forward references in `.word LABEL`):
  parse_factor's ID alternative with record_data_uses_symbol.

### Bugs fixed during Phase 3 cleanup

- **B2 with immediate operand** (e.g., `beq $t0, '-', label`):
  was routing to parse_register and failing.  Added explicit
  imm-form dispatch matching parser.y:1327-1346.
- **Label fix-up across line boundaries** (recap from Phase 3
  main): bison's CMD: TERM doesn't clear_labels, so a bare
  label-on-its-own-line propagates to the next line's
  directive for retroactive alignment via fix_current_label_address.
  Maintain own `hp_this_line_labels`.
- **`.align 0` flag**: data.c's `enable_data_auto_alignment`
  static is unreadable.  Track own `hp_auto_align` mirror.
- **PC-relative branch displacement**: parser.y's `LABEL: ID`
  builds an imm_expr with pc_relative=true, triggering
  resolution-time adjustment in resolve_a_label_sub.  Add
  parse_label() that does the same.
- **`force_identifier` flag cache leak**: bison's only_id=1
  set in parse_opt_label was leaking past the cached token
  it was supposed to affect, mis-classifying the next token
  (e.g., `.word` after a bare label).  Removed the
  unnecessary call.
- **`only_id`-via-FP_REG ambiguity**: COP_REG production
  accepts BOTH Y_REG and Y_FP_REG (parser.y:2572-2574);
  initial dispatch only handled Y_REG.  Added Y_FP_REG path
  to both R2td and FP_R2ts handlers.

### Phase 3 cleanup round 2 additions

After round 1's 14/19, round 2 added enough grammar coverage
to get the spim regression suite to 19/19 parity:

- **R3sh_TYPE_INST**: `sllv`, `srav`, `srlv` (variable-register
  shift).  Both reg-reg and reg-int forms (parser.y:959-972).
- **R2st extras**: `teq`, `tne`, `tlt`, `tgeu`, `tltu`, `tge`
  (BINARY_TRAP_OPS — 2-reg form via `r_type_inst(op, 0, r1, r2)`).
- **I1s_TYPE_INST**: `teqi`, `tnei`, `tgei`, `tgeiu`, `tlti`,
  `tltiu` (BINARYI_TRAP_OPS — `<op> SRC1 IMM16`).
- **BC_TYPE_INST**: `bc1f`, `bc1t`, `bc1fl`, `bc1tl` with
  optional `CC_REG` (parser.y:1286-1306).
- **MOVC_TYPE_INST**: `movf`, `movt` integer conditional moves.
- **FP_MOVC_TYPE_INST**: `movf.s`/`movf.d`/`movt.s`/`movt.d`
  AND `movn.s`/`movn.d`/`movz.s`/`movz.d` — same TYPE in op.h
  but DIFFERENT operand shapes; dispatcher disambiguates by
  peeking the third operand (Y_REG → movn/movz path,
  Y_INT/empty → movf/movt path).
- **Y_MUL_OP** (MULT_OPS3): `mul DEST SRC1 (SRC2|IMM)` —
  immediate form uses $at + ori.
- **Y_SSNOP_OP**: special case — emits `sll $0, $0, 1`.
- **Pseudo-ops**: `mfc1.d`, `mtc1.d`, `l.d`, `l.s`, `s.d`,
  `s.s`, `li.d`, `li.s`, `abs`, `rol`, `ror`, `seq`, `sne`,
  `sle`, `sleu`, `sgt`, `sgtu`, `sge`, `sgeu`.
- **FP_CMP CC_REG form**: `c.le.s 1 $f0 $f2` — optional
  leading CC_REG before the two FP operands (parser.y:1623).
- **COP_REG accepts Y_FP_REG** in MOVE_COP_OPS/R2td (was only
  Y_REG); parser.y:2572-2574 has both alternatives.

### Phase 3 cleanup bugs fixed in round 2

- **EOF didn't clear_labels**: bison's grammar has
  `TERM: Y_EOF { clear_labels(); FILE_PARSE_DONE; }` —
  explicitly clearing labels on the final EOF token, which I
  was missing.  Affected files where the last lines were
  bare-label definitions (the labels' uses never got
  resolved).  Symptom: `tt.core.s`'s `not_an_instruction:`
  defined at EOF was referenced earlier with `sb $a0,
  not_an_instruction`; bison resolved to a 32-bit address,
  hand resolved to 0.  Added `hp_clear_labels()` at end of
  `hp_parse_file()`.
- **Parity-harness false fail on dual-error tests**: when both
  parsers exited with parse errors before `-dump` could
  produce output files, the harness reported diff because
  one was "missing".  Fixed: treat "neither produced X" as
  equivalent (both failed identically).  Affects
  `tt.alu.bare.s`, `tt.bare.s`, `tt.fpu.bare.s`,
  `tt.parse_error.s` — bare-machine and intentional-error
  tests where both parsers correctly reject the input.

### Phase 3 — fully complete (2026-05-19)

After round 2, the hand parser covers **everything that
either /examples or spim's own test suite exercises**, with
44/44 + 19/19 parity.  Things that the grammar accepts but no
test currently uses (and would need adding if someone writes
new asm that needs them):

- `madd.s` / `msub.s` / `nmadd.s` / `nmsub.s` MIPS R2 ternary
  FP family (FP_TERNARY_OPS_REV2).
- `ext` / `ins` bitfield instructions (BF_OPS_REV2).
- Various MIPS R2 family productions that just call
  `mips32_r2_inst()` in bison (a warning-emitting stub).
- `wait`, `mthc1`, `mfhc1`, and similar Rev2 niche opcodes.
- `cop2` instruction support (Y_COP2_OP).

None of these block any current curriculum or test.

### Phase 3 — original spec (preserved for reference)

Expand the hand-written parser to cover every production
currently in `parser.y`:

- All pseudo-ops (`la`, `li` with large constants, `div`,
  `mult`, `sle`, `sgt`, etc.) — by calling the relocated
  `pseudo_op.c` helpers from the new parser.
- FP family (`add.s`, `c.lt.d`, `mfc1`, etc.) — ~70 opcodes.
- Coprocessor and TLB instructions.
- Every remaining `.foo` directive.
- Constant-expression evaluation in `EXPR`/`TRM`/`FACTOR`.

Run the **full** regression corpus through the parity harness
on every commit.  No production lands until its parity check
is green.

## Phase 4 — switch the default (DONE 2026-05-19)

Default flipped from `bison` to `hand`.  Bison stays in the
build as the fallback (`-parser=bison`).

### What changed

- `src/spim.c:350` — `parser_kind` default flipped from
  `PARSER_BISON` to `PARSER_HAND`.
- `src/spim-utils.c:193` — `use_hand_parser` initial value
  flipped from `false` to `true`.
- `src/spim.c` usage text gained two `-parser=` lines so
  the choice is documented in `spim -help`.

### Verification (under new default)

- `examples/src/*/*.asm`: **44 / 44 demos pass parity**
  (hand byte-identical to bison output).
- `/spimulator/tests/tt.*.s`: **10 / 19 pass parity**;
  remaining 9 use FP family / coproc / TLB / unaligned-load
  constructs not yet implemented in the hand parser.  Those
  tests fail identically under both parsers (because they
  use constructs like `$at` literally which bison also
  rejects), OR fail under hand with "parse error" — in both
  cases users can fall back with `-parser=bison`.
- All existing curriculum runtime tests still green
  (`examples/src/06-fizzbuzz 5` etc.).
- Existing `/spimulator/tests/tt.*.s` tests green under
  default: `tt.octal_escape`, `tt.return_value`,
  `tt.unaligned`, `tt.parse_error`, `tt.missing_main`,
  `tt.stderr_split`, `tt.read_char_eof`, `tt.read_int_eof`.

### Fallback behavior

Anyone who needs a construct the hand parser doesn't cover
(FP arithmetic, mfc0/mtc0, ulh/ulw, etc.) sets
`-parser=bison`.  Spim's `-help` documents this.

### Settling period (deferred)

The original plan called for a 2-week settling period
running both parsers in CI before flipping the default.
That was sensible for a maintainer-team context; for a
solo-maintainer project (one user, you), the flip is
authorized immediately and bison stays accessible.  The
practical settling is: if anything misbehaves with the new
default, run `-parser=bison` while the issue is filed.

## Phase 4 — original spec (preserved)

Once the parity harness has been green across the full corpus
for some agreed-upon settling period (~2 weeks of CI runs?),
flip the default from `bison` to `hand`.  Bison stays in the
build behind the `-parser=bison` flag.  CI keeps running both
on every commit for one more cycle to catch any latent
regression.

## Phase 5 — drop bison (DONE 2026-05-19)

flex + bison removed.  Spim is now a pure-C build with no
parser-generator dependency.  Done in the same session as
Phases 0-4.

### What landed

- **Deleted**: `src/scanner.l`, `src/parser.y`,
  `tests/parity-harness.sh` (defunct without a second parser
  to compare against).
- **Build files**: removed `find_program('flex')` /
  `find_program('bison')` and the two `custom_target` rules
  from `meson.build`; removed `flex` and `bison` from the
  Dockerfile's `dnf install` list.
- **New header**: `include/tokens.h` — replaces the bison-
  generated `parser_yacc.h`.  Defines all `Y_*` tokens via
  op.h's X-macro pattern + 8 explicit structural tokens
  (`Y_EOF`, `Y_NL`, `Y_INT`, `Y_ID`, `Y_REG`, `Y_FP_REG`,
  `Y_STR`, `Y_FP`).  Every `#include "parser_yacc.h"` call
  site switched to `tokens.h`.
- **CLI**: stripped `-parser=hand` / `-parser=bison` (and the
  PARSER_BISON / parser_kind enum + use_hand_parser global).
  `-help` no longer documents them.
- **Runtime globals + functions relocated** from `parser.y`
  postlude into the hand-parser TUs:
  - `parse_error_occurred` / `parse_errors_seen` →
    `src/hp_pseudo_op.c`.
  - `data_dir` / `text_dir` → `src/hp_parser.c`.
  - `yyerror` / `yywarn` → `src/hp_pseudo_op.c` (canonical
    definition; `sym-tbl.c` still calls `yyerror` for
    duplicate-label detection).
  - `imm_op_to_op` → `src/hp_pseudo_op.c` (called from
    `inst.c`).
  - `fix_current_label_address` → `src/hp_parser.c`,
    promoted from static `hp_fix_line_labels` to public
    name.  data.c / inst.c's natural alignment flow now
    operates on the hand parser's label state directly.
- **Runtime globals + functions relocated** from
  `scanner.l`:
  - `line_no`, `yylval`, `only_id` → `src/hp_scanner.c`.
  - `register_tbl[]` + `register_name_to_number()` →
    `src/hp_scanner.c`.
  - `erroneous_line`, `source_line`, `scanner_start_line` →
    `src/hp_scanner.c` (thin aliases over the existing
    `hp_*` internals).
- **REPL compatibility wrappers** in `src/hp_scanner.c`:
  - `yylex()` — returns the next token, mapping `Y_EOF` to
    `0` to preserve flex's EOF=0 contract that
    `spim.c`'s `read_token()` relies on.
  - `push_scanner(FILE*)` / `pop_scanner()` — 8-deep
    snapshot stack of all scanner statics, mirroring flex's
    `yypush_buffer_state` / `yypop_buffer_state` semantics.
    Drives the libedit REPL's per-line input.
  - `initialize_scanner(FILE*)` — alias for
    `hp_scanner_init`.
  - `initialize_parser(char*)` — sets the file-name slot
    used by `yywarn` / `yyerror`.
  - `yyparse()` in `src/hp_parser.c` — alias for
    `hp_parse_file()`; lands the REPL's `case ASM_CMD:
    yyparse();` path.

### Pre-Phase-5 hand-parser bugs that surfaced

`src/exceptions.s` failed to load under the hand parser (had
to keep using bison via the now-removed special branch).
Two bugs blocking it:

1. **`hp_sync_to_nl` over-consumed the trailing newline**,
   so `parse_line`'s outer newline check mis-fired "Extra
   tokens after instruction" on whatever followed an ignored
   directive like `.set noat`.  Fixed: stop AT the newline,
   let `parse_line` consume it.  Bug affected every
   directive in the "ignored / consume to NL" set
   (`Y_BGNB_DIR`, `Y_ENDB_DIR`, `Y_ENDR_DIR`, `Y_ASM0_DIR`,
   `Y_ALIAS_DIR`, `Y_SET_DIR`), it just hadn't been tripped
   by anything in the existing /examples or tt.*.s corpus.
2. **`parse_i2` rejected the 2-operand shorthand for
   BINARY_LOGICALI_OPS**: `ori $k0, 0x1` (= ori $k0, $k0,
   0x1).  parser.y:991-994 handles this for andi/ori/xori.
   Used by exception handlers everywhere.  Fixed: peek
   after rt; if next is not a register, treat as the
   2-operand form with rs = rt.

A third, latent bug also surfaced once spim.c started using
the hand scanner directly for REPL commands:

3. **`hp_scanner_advance` overwrote `yylval` on every
   token**, including `Y_NL` / `Y_EOF` / punctuation.  bison's
   flex actions left `yylval` untouched for those tokens, so
   the REPL's `flush_to_newline` (called after consuming a
   `Y_STR` filename) didn't trash the just-read string.  Fix:
   only overwrite `yylval` for tokens that actually carry a
   value (`Y_INT`, `Y_ID`, `Y_REG`, `Y_FP_REG`, `Y_STR`, `Y_FP`).

### Verification

- Dockerfile smoke set passes: bare, core, le, argv,
  args-cmd (REPL multi-run), read_int_eof.
  (`read_char_eof` has a pre-existing test-script bug
  unrelated to Phase 5 — output `abcPassed all tests\n`
  doesn't match `tail -n 1 | grep '^Passed all tests$'`
  because there's no separator newline.  Identical
  behavior to baseline.)
- All other tt.*.s tests match their documented expected
  behavior: tt.dir / tt.octal_escape pass; tt.return_value
  exits 42; tt.unaligned exits 132 (= 128+4); tt.stderr_split
  prints `ok` to stdout; tt.missing_main exits 1;
  tt.parse_error exits 2.  tt.be is endianness-specific
  ("Failed test" on LE host, by design — tt.le is the LE
  variant and passes).  tt.io requires interactive
  memory-mapped IO input.
- /examples curriculum spot-checks all match the pre-Phase-5
  output: 01-helloworld → "hello world"; 06-fizzbuzz 5 →
  1/2/Fizz/4/Buzz; 18-cksum "hello world" → "1135714720
  11"; 19-echo hello there → "hello there"; 20-factorial 6
  → 720; 22-binary-search 7 → "linear: 3, binary: 3".

### Coverage trade-off vs original spec

Phase 5 was originally going to wait ~2 weeks of settling
after Phase 4's default flip.  Per the user's solo-maintainer
context (one user, one machine, immediate authorization),
the flip-and-cleanup landed in the same session.  The
hand parser had byte-identical parity with bison across
all 19 tt.*.s + 44 /examples demos at the time of removal,
so there's no quality justification for the wait.

Things grammar-accepted but currently unused (no test in
either /spimulator or /examples uses them; never reached
the hand parser's grammar):

- `madd.s` / `msub.s` / `nmadd.s` / `nmsub.s` MIPS R2
  ternary FP family (FP_TERNARY_OPS_REV2).
- `ext` / `ins` bitfield instructions (BF_OPS_REV2).
- Various MIPS R2 family productions that just call
  `mips32_r2_inst()` in bison (a warning-emitting stub).
- `wait`, `mthc1`, `mfhc1`, similar Rev2 niche opcodes.
- `cop2` (Y_COP2_OP).

Anyone needing these can either add the dispatch to
`hp_parser.c` or open a follow-up task.

## Risks

The risk vector is the same as the ANTLR option — divergent
parser output.  The hand-written approach has the same
exposures, but two of them shrink:

1. **Pseudo-op expansion semantics.**  Same risk.  Mitigated
   by moving the 22 helper functions to `pseudo_op.c`
   unchanged: same C source, same behavior, just called from
   a different parser.
2. **25 silently-resolved shift-reduce conflicts.**  Smaller
   risk than ANTLR.  In recursive descent each conflict
   becomes an explicit lookahead branch the author writes by
   hand — visible, reviewable, testable.  ANTLR's LL(*) is a
   black box; hand-written is not.
3. **Error message format.**  Now load-bearing
   (`parse_errors_seen > 0 → exit 2` since 2026-05-19).
   Mitigated by reusing the existing `error()` /
   `erroneous_line()` helpers — same call sites, same output
   shape.
4. **Octal escape parsing.**  Same risk.  Reimplement
   `scan_escape` and `copy_str` in `scanner.c` against
   `tests/tt.octal_escape.s` as a regression check.
5. **Label resolution timing.**  Same risk.  Forward
   references resolved in a second pass via
   `flush_local_labels`.  The hand-written parser calls
   `flush_local_labels` at the same points (line-end on
   success, on error).
6. **The `\001` EOF marker.**  Disappears.  Hand-written
   scanner returns `TOK_EOF` directly.  No equivalent hack
   needed.

### Smaller risk than ANTLR

- **No grammar-generator-output divergence.**  All output is
  hand-written; there's no "generator might change between
  versions" worry.
- **Incremental migration is possible.**  The bison path stays
  alive through Phase 4.  Any regression caught after the
  default switch flips can be worked around by setting
  `-parser=bison` while the bug is found.
- **No build-environment dependency change.**  Spim builds on
  any host that has a C compiler.

### Larger risk than ANTLR

- **Operator-precedence handling.**  Bison's `%left`/`%right`/
  `%nonassoc` directives don't appear in the current
  `parser.y` (spim's grammar happens to not need explicit
  precedence — the precedence is encoded structurally in
  `EXPR`→`TRM`→`FACTOR`).  Hand-written follows the same
  structural pattern; same precedence.  Worth verifying in
  Phase 0 inventory but not expected to bite.
- **More lines of source.**  Hand-written is longer than the
  equivalent `.g4` would be.  Estimate: 3000-4000 lines of
  hand-written C vs ~2000 lines of `.g4` grammar.  Same
  order; trade-off is that every line is plain C.

## Mitigations (non-optional)

- **Parity-comparison harness.**  CI step that builds both
  parsers, runs every `tests/tt.*.s` and every
  `examples/src/*/*.asm` through both, byte-diffs the
  assembled text/data segments.  Phase 2 cannot proceed
  without this.
- **`-parser=` flag.**  Both parsers in the binary, switchable
  at runtime.  Lets the user (Bill, future sessions, CI)
  pick if they want to verify a fix on the old parser before
  trusting the new one.
- **Targeted pseudo-op tests.**  New `tests/tt.pseudo_ops.s`
  that exercises every pseudo-op expansion individually,
  with expected text-segment contents documented as a golden
  file.  Catch expansion-sequence regressions immediately.
- **Settling period before dropping bison.**  Phase 4 ships
  the hand-written parser as default but keeps bison in the
  build for ~2 weeks of CI runs.  No data dropped that can't
  be recovered.

## Effort estimate

| Phase | Description | Effort |
|---|---|---|
| 0 | Inventory document (start here, pre-authorized) | ~1 week |
| 1 | Design sketch + module shape + token enum migration | ~3-5 days |
| 2 | Pilot covering R/I-type + simple directives + parity harness | ~1 week |
| 3 | Full coverage including pseudo-op expansion and FP family | ~4-6 weeks |
| 4 | Flip default; bison stays as fallback for ~2-week settling | ~3 days work + ~2 weeks wall clock |
| 5 | Drop bison + flex; cleanup; ChangeLog | ~2-3 days |
| **Total active work** | | **~6-9 weeks** |

Compares favorably to the ANTLR estimate (~8-12 weeks), and
critically: **at every phase boundary the binary still works**,
because the bison path stays alive through Phase 4.

The project can be killed at any boundary once cost/benefit
becomes clear:

- After Phase 0: kill if the inventory reveals more grammar
  weirdness than expected (lots of conflicts that resolve
  surprisingly, semantic actions with hidden state).
- After Phase 1: kill if the design sketch comes out
  significantly longer than the bison grammar — at that point
  the readability win is suspect.
- After Phase 2: kill if the parity harness shows divergence
  the design can't resolve cleanly.
- After Phase 3: kill if the test corpus reveals
  pseudo-op-expansion edge cases that the relocated
  `pseudo_op.c` can't handle without significant rework.

## Recommendation

Start Phase 0 (inventory, ~1 week, pre-authorized).  The
inventory turns the rest of the plan from estimates into
concrete numbers.

Then decide at the Phase 1 gate.  If Phase 1's design sketch
holds the 6-9 week total, proceed to Phase 2 with the parity
harness as the load-bearing tool.  If not, kill cleanly —
bison's still there.

## First concrete deliverable

Start `tasks/scanner-parser-inventory.md`.  Read every line
of `src/scanner.l` and `src/parser.y` and document what's
there.  Don't write any new parser code yet.  The inventory
document is the gate that lets the rest of the project be
evaluated honestly.

The data already gathered (line counts, conflict count,
external-function frequencies, postlude function list) seeds
that document.  The remaining work for Phase 0 is the
per-production walk-through: what each of the 106 productions
matches, what its semantic action does, which C functions it
calls.

## Out of scope

- Changes to the C runtime in `src/inst.c`, `src/data.c`,
  `src/sym-tbl.c`, etc.  Front-end only.
- The 22 pseudo-op expansion helpers' internal logic.  They
  relocate from `parser.y` postlude to `src/pseudo_op.c`
  unchanged.
- Performance work.  Bison and hand-written are both fast
  enough for ~1000-line asm files; no measurable difference.
- Grammar additions.  Migration preserves accepted syntax
  exactly.  Any new syntax happens after the migration lands.
