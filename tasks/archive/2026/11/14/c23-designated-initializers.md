# Write the core's struct tables with designated initializers

**Done:** 2026-11-14. **Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>).
**Branch:** `c23-designated-initializers`. The rule and its checker: `CLAUDE.md` › Conventions, and the
`c-coding-standard.md` reference doc of github.com/billsix/runClaudeInContainer ("Use designated
initializers").

## Outcome

Every struct and union initializer in `src/` and `include/` is designated: the three generated
opcode tables, the register-name table and the pseudo-op description table, 1082 positional rows in
all. `tools/c_designated_init_check.py BUILDDIR src include` lists none.

## Why the earlier C23 passes missed it

The shared standard has asked for designated initializers since before those passes, but no tool
listed the sites. `tasks/archive/2026/11/10/c23-modernization-pass3.md` considered designated
initializers only for the `mips_instruction` decode constructors (opted out, with its reason) and
never scanned the static tables, and clang-tidy's `modernize-use-designated-initializers` reports
nothing for C (clang-tidy 23), so a green lint said nothing either.

## What changed

- **Generated tables.** The C emitter in `tools/gen_opcodes.py` writes `{.name = …, .value1 = …,
  .value2 = …}` for `opcode_tbl`, `opcode_encoding_tbl` and `opcode_decode_tbl`; regenerating
  changed only `src/opcode-tables.c` (1005 rows), not the ports' tables.
- **`src/scanner.c` `register_tbl`.** `{.name = "a0", .value1 = 4}`; the always-zero `value2` is left
  to zero-initialization (35 rows).
- **`src/explain.c` `pseudo_ops`.** `{.name = …, .what_it_means = …}` (41 rows plus the sentinel,
  now `{.name = nullptr, .what_it_means = nullptr}`, which `find_pseudo` stops on). The deeper
  indent made clang-format split description literals before their last word, so each literal over
  the width was first split at its midpoint; the concatenated strings are byte-identical to before
  (checked by parsing both versions), so the teaching output is unchanged.
- **The checker.** The discovery scan became `tools/c_designated_init_check.py`, a shared, vendored
  tool: canonical in github.com/billsix/runClaudeInContainer `tasks/reference/`, mirrored in
  runCrushInContainer, kept byte-identical like `c_dispatch_check.py`. It walks clang's JSON AST
  over a compile database, reports each positional struct/union `InitListExpr` in the given paths,
  accepts a site marked `designated-init-check: ok`, and exits 1 when any are left. No meson test
  runs it yet.
- **Shared layer** (staged in both sandboxes for the maintainer to commit): `c-coding-standard.md`
  names the checker at the rule, `c-modernization-sweep.md` says to run the rule checkers first,
  `repo-review-conventions.md` (dimension 4) and the `audit-repo` command say an audit runs the
  standard's checkers and reports every site as a finding, and both sandboxes' `CLAUDE.md` list the
  new tool.

## Decisions

- **`.value1` / `.value2` designators, not renamed fields.** `name_val_val` is shared by four
  tables whose two integers mean different things (token and type, token and encoding, encoding
  and token, register number); renaming its fields was out of scope.
- **Scope stayed `src/` + `include/`.** The scan found no positional record initializer in
  `examples/` or `pgu/`, which opt out of designated initializers anyway
  (`tasks/reference/example-c-teaching-style.md`).
- **The checker is not gated.** Wiring a meson test like `dispatch_check` is a follow-up for the
  maintainer to approve; the tree is clean, so it would pass today.

## Verification

- `tools/c_designated_init_check.py` over a clang build of all 103 translation units: 0 sites
  (1082 before). On a synthetic file it reports positional rows and skips designated, `{}` and
  marked ones.
- `tools/gen_opcodes.py --check`: current. `tools/c_dispatch_check.py BUILDDIR src`: clean;
  clang-tidy on the three changed files: no warnings.
- Host clang build `-Werror` and the `regression` suite: 53 passed.
- `make image MINIMAL_IMAGE=1 RUN_SANITIZERS=1` (the CI gate: every meson suite and the UBSan/ASan
  runs): passed; meson 348 ok, 0 failed, and the regression suite 53 ok under each of UBSan-trap
  and ASan.
