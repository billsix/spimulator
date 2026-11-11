# Cleaned up the Python port against the Python standard

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Source:** tasks/review/2026/11/11/repo-review.md §4.3.3, §4.3.4, §4.3.5, §4.3.6, §4.3.7, §4.3.8 F19

## Summary

No output changed. A fresh subagent did the work with git forbidden, and the coordinator reviewed
and committed it. The files are under `port/python/spimulator_py/`.

- **Guard-only `match`es** (`parser.py` `_parse_factor`/`_parse_address`, `scanner.py`
  `_scan_one_token`) are if/elif/else chains on one peeked token. A dotted value pattern did not
  fit, because the arms compare with `ord("(")` and the like.
- **Total dispatch:** `_emit_one` gained `case Kind.FILE:` (raises, since the one FILE node is the
  root) and `case _: assert_never(...)`; `ast_nodes._imm_text`, `symbol_table._compensate_lui` and
  `spim.py`'s ARGS loop end in an `else`. The catch-alls that stood for one member name it, with a
  raising default: `_expand_set` (SEQ|SNE), `_expand_compare_branch` (BLEU), `_logic_immediate`
  (XORI), `_store_integer` (SW|SC), `_fp_convert` (CVT_S_W), `_fp_move` (MOV_S|MOV_D). Each caller
  was checked to dispatch exactly those opcodes. The REPL's unknown-command `case _` is a real
  default and stayed.
- **Tokens, not strings:** `_executes(*tokens: Tok)` (47 decorators, by codemod); the `Tok[f"..."]`
  loops and mnemonic slicing became token tuples and token-keyed tables. The FP-compare rows are
  chosen by `OpType.FP_CMP_FS_FT_INST` and the fmt field of the encoding, checked to pick the same 32
  rows. The REPL's command variables are typed `Command`. `run.py` can't read
  `instruction._COMPARE_CONDITIONS` at import time (a partially initialized module), so it uses the
  encoding filter.
- **Dead code:** `options.force_break` and `Kind.ERROR` (the last member, so no value shifts) were
  removed, along with seven genuinely unused `noqa`s. The review's count of 10 came from
  `--select RUF100`, which turns off the configured rules; the SIM115 ones are in use.
  `ruff check --extend-select RUF100` is clean.
- **Layering:** the local imports in `scanner.py` and `spim_utils.py` became module-level, because
  no cycle existed. `console.py`'s `import termios` is a platform import and stayed. The 12 runtime
  `assert`s now raise explicitly, so `python -O` keeps them.
- History comments in `run.py` and `instruction.py` were rewritten to describe the present.

The two one-shot codemods (`executes_tokens.py`, `asserts_to_raises.py`) were committed under
`tasks/adhoc/python-port-cleanups/` with the work and removed with this archive. They remain in git
history.

Gates: `port/python/lint.sh` (ruff check and format, `ty`, `check_local_annotations`, 38 doctests);
`meson test --suite port-python`, 82/82 (comparisons with C included).
