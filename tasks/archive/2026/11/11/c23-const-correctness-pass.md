# C23 modernization pass 4: `const` wherever it can go in `src/` and `include/`

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `c23-const-correctness-pass`
**Follows:** `tasks/archive/2026/11/10/c23-modernization-pass3.md`

## Outcome

The simulator core now has `const` on every binding that never changes and that C allows to be
`const`, following the "`const` by default" rules of the `c-coding-standard.md` reference doc in
github.com/billsix/runClaudeInContainer. Six commits on the branch, one per kind of binding:

| Commit | What became const |
|---|---|
| const pass 1 | The file-scope tables `spim_commands`, the two completion `cmds` tables (`const char* const`), `op_type_table`, `register_tbl` and `keyword_tbl`. For the last two, `map_string_to_name_val_val` and `map_int_to_name_val_val` take a const table and key and return a const entry, and their nine callers hold a `const name_val_val*`. |
| const pass 2 | 58 pointer parameters never written through (`const T*`), with their prototypes in `include/` and forward declarations in `src/`. Among them are `find_symbol_address(const char*)`, the `explain_*` entry points and `copy_inst`. |
| const pass 3 | `DEFAULT_RUN_LOCATION` and `END_OF_TRAP_HANDLER_SYMBOL`, deferred by pass 3, are `constexpr char[]` now that `find_symbol_address` takes `const char*`. |
| const pass 4 | About 120 pointer locals never repointed (`T* const p`). |
| const pass 5 | About 300 scalar locals assigned only at their declaration. MADD/MSUB's `product_low` is split out of its declaration with `product_high`, which changes. The locals inside `MIN`, `MAX`, `ROUND_UP` and `ROUND_DOWN` are const. |
| const pass 6 | explain.c's `special_funct_to_name` and `regimm_rt_to_name` tables (`const char* const`). |

Behavior is unchanged, and was shown four ways at every commit:
- clang and gcc debug builds at `-Werror` under `-Wconversion -Wsign-conversion`. Writing a const
  object, or dropping a qualifier, is a compile error, so this is the main proof.
- The regression suite.
- A comparison of every object's machine code at `-O2` against the tree before the pass
  (`compare_text.sh`). Every object's `.text` is byte-identical except `spim.c`. There clang
  scheduled three whitespace comparisons in `spim_completion` in another order once it knew the
  `cmds` tables were constant; the instruction lists, sorted, are identical. Pass 3 also changed
  relocation offsets into the string pool.
- clang-tidy with the project's `.clang-tidy`: no warnings on the changed files.

The full `meson test` (283 OK, including the teaching-mode goldens and the three port suites) and
`make image` (sanitizers on) passed on the final commit. The ports needed no change.

## How

The candidates came from a scan of clang's JSON AST, `const_candidates.py`, because clang-tidy's
`misc-const-correctness` checks C++ only and reports nothing on C. A codemod, `apply_const.py`, then
did three things:
- inserted `const` at every candidate;
- built with clang `-Werror`, and put back each candidate that a compile error named;
- repeated from a fresh scan until a run kept nothing new. A `const` on a callee can make its
  callers' parameters possible.

Clean builds are the proof that each insertion is sound. Formatting was applied to the changed lines
only (`format_changed.sh`), because the tree has formatting differences that predate this pass.

Two scan bugs were found and fixed during the pass:
- **Wrong function blamed for errors.** The regex that finds an error's enclosing function also
  matched indented calls, so errors were pinned on the wrong function. The first run stopped after
  60 builds; its partial edit was reverted before the re-run.
- **Pointer locals missed.** The scan read `const char *p` as already const, though only the
  pointee is, and so skipped 45 pointer locals.

## Not const, with the reason

- **44 pointer parameters the compiler rejected.** Nearly all pass the pointer on to, or store it in,
  something that is not const: for example `ss_lazy_init`'s `str_stream*`, or a callback whose
  function type is fixed. The list with each compiler message was in
  `tasks/adhoc/c23-const-correctness-pass/data/rejected-param-*.txt` (removed at archive, so see
  the archive commit's parent).
- **Skipped by rule:**
  - `FILE*` parameters, because stdio takes `FILE*`;
  - `T**` parameters, because `T**` does not convert to `const T**` in C;
  - function pointers.
- **`name_tbl`, `i_opcode_tbl`, `a_opcode_tbl`:** `initialize_inst_tables` sorts them with `qsort` at
  startup.
- **Opt-outs from the coding standard and the `c-modernization-sweep.md` reference doc:**
  - the setjmp functions `main` and `top_level`;
  - loop and list cursors;
  - address-taken locals;
  - top-level `const` on parameters (not part of the caller's contract).

## Tools

- **Kept:** `tools/const_candidates.py`, the scan, promoted at archive so new code can be checked
  the same way.
- **Removed at archive** (one-shot): `apply_const.py`, `file_scope_candidates.py`, `compare_text.sh`
  and `format_changed.sh`, from `tasks/adhoc/c23-const-correctness-pass/`.
