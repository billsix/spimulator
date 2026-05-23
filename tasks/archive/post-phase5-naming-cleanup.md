# Task: post-Phase-5 naming + comment cleanup

## Goal

Now that flex+bison are gone (see
[`handwritten-parser-migration.md`](handwritten-parser-migration.md)
Phase 5), drop the naming baggage that survived the migration
as compatibility shims or "during coexistence" markers.  Make
the code read like it was always pure C, not like the residue
of a multi-phase port.

The hand parser is now THE parser.  The `hp_` prefix is
redundant.  `yylex` / `yyparse` / `Y_*` token names exist only
because the bison surface used to.  Many doc comments still
narrate the migration that is now history.

## Scope and non-scope

**In scope** (pure renaming + comment edits — no behavior
change):
- Identifier renames in src/hp_*.c, include/hp_*.h,
  include/scanner.h, include/parser.h, include/tokens.h
- Caller updates in src/spim.c, spim-utils.c, sym-tbl.c,
  data.c, inst.c, explain.c, run.c (wherever the renamed
  symbols are called).
- Stale-comment sweep across all touched files.

**Out of scope**:
- Any change to assembly syntax, syscall numbers, register
  conventions, REPL command vocabulary.
- Behavior changes to the parser or scanner.
- Re-organising file boundaries (don't merge hp_scanner.c
  into hp_parser.c, don't split out a new module).
- include/op.h X-macro structure (the OP() entries
  themselves stay; only the names of tokens they expand to
  might change in Tier 2c, and even then via a sed-able
  rename of `Y_` to `TOK_`).

## Verification gate (applies to EVERY tier)

After each tier completes:

1. `ninja -C builddir` clean.
2. Dockerfile smoke set passes — bare / core / le / argv /
   args-cmd / read_int_eof (see ChangeLog
   2026-05-19 entry for the canonical incantations).
3. /examples spot-checks produce identical output to
   pre-tier output: 01-helloworld, 06-fizzbuzz 5,
   18-cksum 'hello world', 19-echo hello there,
   20-factorial 6, 22-binary-search 7.

If any tier breaks (2) or (3), back it out — don't try to
fix forward.  Cosmetic cleanups have no business causing
regressions.

## Tier 3 — stale comment sweep (start here)

Effort: half a day.  Risk: zero (comments only).
Payoff: high — code reads honestly.

Files that still reference the migration as if it's
in progress (per audit 2026-05-19):

```
src/hp_parser.c
src/hp_pseudo_op.c
src/hp_scanner.c
include/hp_parser.h
include/hp_pseudo_op.h
include/scanner.h
include/parser.h
include/tokens.h
```

Patterns to retire:

- `"during coexistence"`, `"during Phases 2-4"`,
  `"Phase 5 will merge"`, `"Phase 5 cleanup"` — the
  migration is done.
- `"bison's static this_line_labels"`, `"bison's only_id"`,
  `"bison side"`, `"the bison path"` — there is no bison.
- `"see tasks/handwritten-parser-migration.md"` — fine if
  the comment is documenting historical context; remove if
  the comment is "future work".
- Sample bad comment (hp_parser.c near hp_clear_labels):
  `"Phase 5 cleanup will merge with parser.y's static."`
  → just delete; the static IS the only one.
- Sample bad comment (hp_scanner.c top):
  `"Replaces src/scanner.l during Phases 2-4 of the parser
  migration."` → rewrite to one line of provenance:
  `"Hand-written lexical scanner."`.

Approach: read each file top-to-bottom once.  For each
"during" / "Phase" / "coexist" / "bison" / "scanner.l"
mention, decide: delete, rewrite as plain context, or
keep (if it documents a non-obvious WHY that's still
true).  Default to delete.

First concrete step: `grep -n 'bison\|Phase [0-9]\|coexist\|parser\.y\|scanner\.l' src/hp_*.c include/hp_*.h include/scanner.h include/parser.h include/tokens.h` — produces the punch list.

Gate before Tier 2: re-run verification.  Tier 3 is a
defensible stopping point on its own.

## Tier 2a — drop the `hp_` prefix

Effort: ~1 day.  Risk: low (mechanical, the verification
gate catches anything broken).  Payoff: high — names
match what the code actually is.

The `hp_` prefix made sense when two parsers coexisted.
Now it's just clutter.

Rename targets (~370 occurrences across 5 files):

| Old name | New name |
|---|---|
| `hp_scanner_init` | `scanner_init` |
| `hp_scanner_peek` | `scanner_peek` |
| `hp_scanner_peek2` | `scanner_peek2` |
| `hp_scanner_advance` | `scanner_advance` |
| `hp_scanner_next` | `scanner_next` |
| `hp_scanner_start_line` | `scanner_start_line` (already a public alias — merge) |
| `hp_scanner_force_identifier` | `scanner_force_identifier` |
| `hp_token` | `token` (or `scan_token` if `token` clashes) |
| `hp_parse_file` | `parse_file` |
| `hp_initialize_parser` | `initialize_parser` (already a public alias — merge) |
| `hp_yyerror` | `parse_error_at` (internal helper) |
| `hp_input_file_name`, `_get`, `set_` | drop prefix; expose as a plain `parser_input_file_name` global with a public getter |
| `hp_this_line_labels`, `hp_label_cell` | `this_line_labels`, `label_cell` |
| `hp_clear_labels`, `hp_cons_label`, `hp_fix_line_labels` | drop prefix |
| `hp_align_labels_to`, `hp_auto_align` | drop prefix |
| `hp_store_op`, `hp_store_fp_op`, `hp_null_term` | drop prefix |
| `hp_sync_to_nl` | `sync_to_nl` |
| `hp_check_keyword`, `hp_keyword_tbl` | `check_keyword`, `keyword_tbl` |
| `hp_check_imm_range`, `hp_check_uimm_range` | drop prefix |
| `hp_div_inst`, `hp_mult_inst`, `hp_set_*_inst` | drop prefix |
| `hp_nop_inst`, `hp_trap_inst`, `hp_branch_offset` | drop prefix |
| `hp_op_to_imm_op` | drop prefix (already coexists with `imm_op_to_op` as the canonical name; merge to one symbol) |
| `hp_imm_op_to_op` | merge with `imm_op_to_op` (same body) — see Tier 2a side-task below |
| `hp_erroneous_line` | drop prefix; `erroneous_line` is already the public alias |

File renames (Git mv):

| Old | New |
|---|---|
| `src/hp_scanner.c` | `src/scanner.c` |
| `src/hp_parser.c` | `src/parser.c` |
| `src/hp_pseudo_op.c` | `src/pseudo_op.c` |
| `include/hp_parser.h` | merge into `include/parser.h` |
| `include/hp_pseudo_op.h` | new `include/pseudo_op.h` (or merge) |

Update `meson.build`'s source list accordingly.

### Tier 2a side-task: dedupe imm_op_to_op / hp_imm_op_to_op

Both functions have identical bodies (cross-referenced in
the ChangeLog 2026-05-19 entry on Phase 5).  They exist as
two symbols because Phase 5 needed to expose
`imm_op_to_op` to inst.c without renaming the existing
`hp_imm_op_to_op` callers in hp_parser.c.  Now both are in
the same TU (after the file rename); collapse to one.

### Tier 2a approach

1. Generate a sed script from the rename table above.
2. Apply it across all .c and .h files.
3. Git mv the files.
4. Update meson.build sources list.
5. Build clean.  If the verification gate passes, commit
   as ONE atomic rename commit so future archaeology
   doesn't have to track partial renames.

Gate before Tier 2b: re-run verification.

## Tier 2b — drop the bison/flex API names that the REPL uses

Effort: half a day.  Risk: low.  Payoff: medium.

`src/spim.c`'s REPL command tokenizer calls `yylex`,
`yyparse`, `push_scanner`, `pop_scanner`,
`initialize_scanner`, `initialize_parser`, `flush_to_newline`.
The first six exist as compatibility shims in `hp_scanner.c`
/ `hp_parser.c`.  After Tier 2a these shims have been
deduped; now rename them to non-yacc-era names.

| Old name | New name |
|---|---|
| `yylex` | `scanner_next_token` |
| `yyparse` | `parse_program` (or `parse_repl_command` if it always covers a single REPL command) |
| `yyerror` | `parse_error` (public, used by sym-tbl.c too) |
| `yywarn` | `parse_warn` |
| `push_scanner` | `scanner_push_source` |
| `pop_scanner` | `scanner_pop_source` |
| `initialize_scanner` | `scanner_init` (after Tier 2a this is the canonical name; merge with the alias) |
| `initialize_parser` | `parser_init` |

Update every call site in src/spim.c, src/spim-utils.c,
src/sym-tbl.c.

`include/scanner.h`'s `#define YYSTYPE yylval_t` macro is
pure bison-interop sediment with no remaining bison —
delete the `#define`, leave the `typedef` (renamed per
Tier 2c).

Gate before Tier 2c: re-run verification.

## Tier 2c — rename `Y_*` tokens to `TOK_*`

Effort: ~1 day.  Risk: medium (largest blast radius —
390 token names, 461 use-sites across runtime + parser).
Payoff: medium.

The `Y_` prefix means "yacc-style token" — historical.
Rename to a self-explanatory prefix.  Two options to
pick from at the start of Tier 2c:

- **Option A**: `TOK_*`.  Short, conventional.
  `Y_ADD_OP` → `TOK_ADD_OP`, `Y_INT` → `TOK_INT`, etc.
- **Option B**: `T_*`.  Even shorter.  Risk of one-letter
  prefix collision is low (no other `T_` symbols in the
  tree).

Recommendation: Option A — `TOK_` is unambiguous and
greppable.

Approach: pure sed across all .c, .h, .y-leftover-doc
files.  The `Y_*` set is mechanical (every match is the
rename target).  The grammar-vocabulary uppercase pattern
makes it unambiguous.

Also rename:
- `yylval` (the global) → `scan_value`
- `yylval_t` (the type) → `scan_value_t`
- `YYSTYPE` macro — already deleted in Tier 2b

`include/tokens.h` becomes the home of the new `TOK_`
identifiers (the `enum { ... }` body changes; the X-macro
in `op.h` doesn't need to change because tokens.h is what
expands it).

Gate before stopping: re-run verification.

## Tier 2d — delete the `only_id` shim

Effort: ~10 minutes.  Risk: low.  Payoff: small (one
honest symbol fewer).

`only_id` is a global declared in `include/scanner.h` and
defined in `src/hp_scanner.c` as a Phase-5 compatibility
courtesy for any caller that "still inspects it".  The
hand scanner uses `force_id_next` internally and doesn't
read `only_id`.

Verify no caller reads it:

```
grep -nrE '\bonly_id\b' src/ include/
```

If the only mentions are the declaration + definition (no
reads, no writes from outside the scanner), delete both.

If a reader exists (most likely candidate: `explain.c`
inspecting the flag for diagnostics), evaluate
case-by-case — either remove the reader's call or expose
a proper accessor.

## Open question: keep `Y_` or rename?

Tier 2c is the only Bill-bikeshed call in this plan.
Three reasonable positions:

- Drop `Y_` — it's a yacc artifact, the names should be
  honest.
- Keep `Y_` — it's recognisable to anyone who's read
  bison output, and renaming touches 461 sites.  Cosmetic
  cost / pedagogical value of `TOK_` is small.
- Compromise — rename only the special tokens (`Y_INT`,
  `Y_ID`, `Y_REG`, ...) and leave the 381 opcode tokens
  (`Y_ADD_OP`, ...) alone since they're machine-generated
  from op.h anyway.

Decide at the Tier 2c gate, not now.

## Authorization

- **Tier 3** (comment sweep): pre-authorized.  No
  behavior change possible.
- **Tier 2a** (drop `hp_`): authorize after Tier 3 lands
  and the verification gate passes.
- **Tier 2b** (rename yylex/yyparse/etc.): authorize
  after Tier 2a.
- **Tier 2c** (rename Y_*): authorize after Tier 2b;
  bikeshed the prefix choice at that gate.
- **Tier 2d** (delete only_id): authorize independently;
  trivial.

Stop at any tier boundary that feels like enough.
Each tier leaves the binary in a better state than it
found it.

## Why not do it all in one pass

- A 600+ line diff that touches 15 files is unreviewable
  in one sitting.
- The verification gate is more meaningful per-tier — if
  Tier 2c breaks something, you want to know it was Tier
  2c, not "one of the four tiers".
- Tier 3 (comments) and Tier 2d (only_id) are independent
  of each other and the renaming tiers; they can land
  whenever.

## First concrete step

`grep -nE 'bison|Phase [0-9]|coexist|parser\.y|scanner\.l|"hp_"' src/hp_*.c include/hp_*.h include/scanner.h include/parser.h include/tokens.h` — produces the Tier 3 punch list.  Sweep that.  Stop.  Run the verification gate.  Then decide whether to authorize Tier 2a.
