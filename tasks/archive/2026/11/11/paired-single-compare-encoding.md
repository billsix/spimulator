# The paired-single compares (`c.cond.ps`) were mis-encoded and couldn't run — removed

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `dispatch-follow-ups`
**Found by:** `tasks/archive/2026/11/11/c-switch-and-else-if-chains.md`, while adding a `default` to
`r_cond_type_inst`'s switch (`src/instruction.c`)

## Outcome

The 16 `c.cond.ps` rows of `include/opcodes.tsv` are gone.
- **What was wrong:** they encoded format field 0x13, where MIPS32 Release 2 and GNU `as` put
  paired-single at 0x16 (`c.eq.ps $f0, $f2` should be `0x46c20032`; spim gave `0x46620032`). They
  had no `COND` case in `r_cond_type_inst`, and the engine couldn't execute them ("Unknown
  instruction type").
- **After:** `python3 tools/gen_opcodes.py` (the payload of `make generate`) regenerated the C
  engine's tables, the three ports' tables and tree-sitter's keyword lists. A `c.eq.ps` line is now
  "Instruction not yet supported", as any unsupported mnemonic is. Tokens after the removed rows
  renumber, and no golden depended on their numbers.
- **The fallthrough is now fatal:** only the 32 `.s`/`.d` compares can reach `r_cond_type_inst`,
  so its fallthrough is a fatal internal error in all four engines. Before, it had diverged:
  C and Rust left `COND` unset, Python skipped it, and Haskell set 0.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. Remove the rows rather than implement paired-single compares. Nothing in `examples/` or
   `tests/` used them, and the engine couldn't run them.
2. `r_cond_type_inst`'s fallthrough becomes fatal.

## Found on the way

The table's other 22 paired-single rows have the same problem: they assemble, but none of them run,
and `abs.ps`, `add.ps` and `sub.ps` have the same wrong format field. This task's decision covered
only the compares, so that work is `tasks/paired-single-instructions-unsupported.md` (proposed).

## Verification

- `tools/gen_opcodes.py --check` is clean.
- Every meson suite passes, 293/293: regression, `generated_opcode_tables`, examples, port-python,
  port-haskell with its annotation check, and port-rust.
- gcc and clang `-Werror` builds are clean. `hlint`, clippy, `cargo fmt` and ruff are clean.
- The image gate, with `BUILD_TREE_SITTER=1` (its default), passed at the end of the branch: 293/293
  meson tests.
