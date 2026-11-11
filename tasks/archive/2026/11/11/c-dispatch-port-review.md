# Review the C dispatch changes for anything the Python, Haskell and Rust ports should follow

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `dispatch-follow-ups`
**Follows:** `tasks/archive/2026/11/11/c-switch-and-else-if-chains.md`

## Outcome

The C engine's dispatch work changed no behavior, so the port suites never required anything. This
review checked each C change against the three ports and made the structural changes that carry
over. Each item below says what changed, or why nothing needed to.

**1. Fatal defaults on exhaustive switches.**
- The teaching mode isn't ported, so the `explain.c` sites (the decode-step renderers,
  `subst_field_regs`) have no counterpart.
- **Python:** the matches over a whole enum already ended in `case _: raise AssertionError(kind)`.
  The three that match every member are now `assert_never(kind)`: two in `ast_nodes.py`, and
  `_listing_observer`. `ty` 0.0.91 then checks them statically, the Python counterpart of C's
  `-Wswitch-enum`. Deleting a case from a scratch copy makes `ty` fail with
  `type-assertion-failure`.
- **Rust and Haskell:** they match on the AST and event enums exhaustively, so their compilers
  check this already. The byte-lane cases are computed shifts there, not switches.
- `parse_j`'s and `r_cond_type_inst`'s fallthroughs were already made fatal in all four engines
  (`tasks/archive/2026/11/11/parse-j-drops-cop2-and-jalr-hb.md`,
  `tasks/archive/2026/11/11/paired-single-compare-encoding.md`).

**2. Legitimate defaults.** `explain_before`, `explain_after` and `lookup_classification` are teaching
mode, so not ported. `parse_j` and `r_cond_type_inst` are covered in item 1.

**3. One-value chains and guard runs.**
- **Python:** done before the C work (`tasks/archive/2026/11/11/python-port-match-statements.md`),
  which the C work copied.
- **Rust:** not needed. The Rust standard leaves early returns idiomatic and puts total dispatch on
  exhaustive `match`; its one-value dispatches already are `match`es.
- **Haskell:** not applicable. An `if` always has an `else`, and `IO` has no early return, so a
  "run of early-exit `if`s" can't be written.

**4. Explicit `else` branches.**
- `store_instruction`'s impossible "neither segment active" case is now a fatal internal error in
  all three ports, with C's message.
- **Python:** the seven chains that mirror C's open-ended ones now end in a commented
  `else: pass` naming the remaining outcome: `_format_imm_expr`, `check_memory_mapped_io` (×2),
  `mult_inst`, `_branch`, `_append`, `_parse_address`'s offset, and `parse_line`'s EOF. Two C sites
  have no Python chain of that shape: the FCR write in `ctc1` is a single combined `if`, and
  `_resolve_a_label_sub` is structured differently.
- **Rust:** not done. An empty `else {}` runs into clippy's `needless_else` lint, and the Rust
  standard's tool for total dispatch is `match`.
- **Haskell:** not applicable; `if` requires an `else`.

**5. `-Wswitch-enum`'s equivalent, per port gate.** Python: `ty` with `assert_never` (item 1). Rust:
`match` exhaustiveness is a compile error. Haskell: `-Wall -Werror` includes
`-Wincomplete-patterns`.

## Verification

- port-python, port-haskell (with its annotation check), port-rust and the regression suite:
  258/258.
- ruff, `ty`, `cargo fmt`, clippy and `hlint` are clean, with `hlint`'s count unchanged.
