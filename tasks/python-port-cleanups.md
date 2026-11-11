# Python port cleanups from the 2026-11-11 review

**Status:** proposed — needs go-ahead
**Priority:** 7
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §4.3.3, §4.3.4, §4.3.5, §4.3.6, §4.3.7, §4.3.8 F19

## BLUF

Code-quality items in `port/python/spimulator_py/` against the Python standard (the
python-coding-standard.md reference doc from github.com/billsix/runClaudeInContainer). None changes
behavior. Done means each is fixed, ruff and `ty` are clean, and `port-python` is green.

## Context

- **Guard-only `match`es** (§4.3.3): `parser.py:287` (`_parse_factor`), `parser.py:397`
  (`_parse_address`) and `scanner.py:471` use `case _ if scanner.peek() == TOK_INT:` in every arm,
  because `TOK_*` are bare names that `match` would treat as captures. Match on `opcodes.TOK_INT`
  (a dotted value pattern), or write an if/elif/else chain.
- **History comments** (§4.3.4): `run.py:957` and `instruction.py:622` say "as before". In
  `port/python/spimulator_py/instruction.py` the case is "other major opcodes: the top 6 bits alone are the key".
- **Open-ended dispatch** (§4.3.5):
  - `_emit_one` (`parser.py:1313`) has no `case _`/`assert_never`;
  - `else`-less chains at `ast_nodes.py:126`, `symbol_table.py:249` and `spim.py:829`;
  - catch-alls that stand for one member: `parser.py:941`, `parser.py:1011`, `run.py:403-415`,
    `run.py:822-833`.
- **Strings as options** (§4.3.6): `_executes(*names: str)` uses `Tok[f"{name}_OPCODE"]`, and
  `run.py:684,691,853,886,990,1072` slice mnemonic text. The REPL `Command` values are typed `int`
  (`_prev_cmd`, `_COMMANDS`).
- **Dead code** (§4.3.7): `options.force_break` is only written (`run.py:120`); `Kind.ERROR` is never
  constructed (`ast_nodes.py:214,338`, `parser.py:1409`). `ruff --select RUF100` finds 10 unused
  `noqa` comments: `spim.py:180,316,406,697,920`, `parser.py:749,1308`, `ast_nodes.py:107`,
  `spim_utils.py:159`, `__init__.py:19`.
- **Layering** (§4.3.8): function-local imports break cycles (`scanner.py:252,274,401`,
  `spim_utils.py:143`), and `assert` is used as a type guard (`parser.py:955,1395-1399`, `port/python/spimulator_py/scanner.py`),
  which `python -O` strips.

## Plan

1. Fix each group; run ruff, `ty` (from `port/python/`) and the annotation check.
2. Gate: `meson test --suite port-python`, and the harness.

## Open questions

None.
