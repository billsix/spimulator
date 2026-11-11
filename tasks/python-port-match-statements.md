# Use `match` for the Python port's dispatch chains, and `elif` for its guard runs

**Status:** ready — decision made, implementation not started
**Priority:** 6
**Difficulty:** 4
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)

## BLUF

Fifteen if/elif chains in the Python port pick a branch by comparing one value against a list of
constants, the shape a C `switch` has. Rewrite each as a `match` with a `case _` default, as the
Python coding standard asks. Most of these chains compare against string-keyed dict lookups
(`TOK["LI_PSEUDO_OP"]`) or bare names, which a `case` cannot spell, so the opcode constants must
first become dotted names: members of the generated `Tok` and `OpType` `IntEnum`s, which exist
since 2026-11-11 (Decisions). Also, 42 runs of separate early-exit `if`s are one decision each:
7 dispatch on one value and become `match`es too, and the other 35 become `if`/`elif`/`else`
chains, as the standard's "Mutually exclusive branches" rule asks. Done means each chain and run
is converted or listed with the reason it stays as it is, and the port suites still pass.

## Context

- **The rules:** the `python-coding-standard.md` reference doc of
  github.com/billsix/runClaudeInContainer.
  - "Prefer `match` + `case _` over an open-ended `if`/`elif` chain": always write `case _`. A
    `match` whose every case is a guard is an `if` chain in disguise.
  - Rule (C) of "Refactoring a conditional": use `match` when the pattern does the dispatch.
  - "Options are enums, not strings."
  - Annotate every binding.
- **The worklist** is `tasks/adhoc/python-match-statements/data/chains.txt`, from
  `tasks/adhoc/python-match-statements/match_candidates.py`. The script scans `port/python`,
  `port/harness`, `tools` and `tree-sitter/scripts`, and lists chains where 3 or more tests compare
  one subject. The format is `file:line: matching/all links on SUBJECT (classes) [else]`.
  - `value`: the compared values are literals or dotted names, usable in a `case` today.
  - `lookup`: they are dict lookups or bare names, which need the constants changed first.
- **What it found:** 14 chains, all in `port/python/spimulator_py/`, plus one the script misses:
  `ast_nodes.py:136` tests `byte in b'\\"'`, which is a `case 0x5C | 0x22:`.
  - **Usable today (`value`):**
    - `spim.py:203`: the command-line flags, 29 of 32 links;
    - `instruction.py:621`: no `else`;
    - `memory.py:422`;
    - `run.py:941`: no `else`.
  - **Need dotted constants (`lookup`):**
    - `parser.py:598` on the operand kind, 22 links;
    - `parser.py:714` (`_expand_pseudo`, 20 links), `parser.py:895`, `parser.py:956`;
    - `parser.py:1093` on the directive, 16 of 17 links;
    - `instruction.py:554` on the operand kind, 15 links;
    - `run.py:1072`;
    - `spim.py:677` on the REPL command, 17 links;
    - `spim.py:725`;
    - `scanner.py:317`.
- **Guard runs** (added 2026-11-11 at the maintainer's request). The standard's "Mutually exclusive
  branches are one `if`/`elif`/`else` chain" asks for a run of early-exit guards in front of a
  result to be one chain, with the result in the `else`. It leaves two things as they are: a single
  guard before the main body, and independent checks that can each fire.
  - `tasks/adhoc/python-match-statements/elif_chain_candidates.py` lists every run of 2 or more
    consecutive `if`s with no `else` whose bodies end in `return`, `raise`, `continue` or `break`.
    Because each one exits, they cannot both fire, so a run is always one decision.
  - The worklog is `tasks/adhoc/python-match-statements/data/guard-runs.txt`: 42 runs. By file:
    `memory.py` 10, `parser.py` 9, `scanner.py` 5, `spim.py` 4, `machine.py` 4,
    `instruction.py` 3, `port/harness/compare_engines.py` 2, and one each in `syscall.py`,
    `ast_nodes.py`, `tools/check_example_commands.py`, `tools/check_asm_canonical.py` and
    `tree-sitter/scripts/build-keyword-lists.py`.
  - **Seven of them compare one subject in 3 or more ifs and become `match`, not `elif`.** They are
    dispatch written as separate `if`s, which the chain scan cannot see:
    - `instruction.py:641` (14 ifs on `kind`) and `instruction.py:704` (24 ifs on `kind`);
    - `scanner.py:466` (8 on `char`), `memory.py:456` (4 on `addr`), `parser.py:444` (4 on `op`);
    - `parser.py:286` and `parser.py:392` (3 on `scanner.peek()`).
  - The other 35 become `if`/`elif`/`else`.
- **No other dispatch chains.** The executor already dispatches through dict tables
  (`run.py` `_FACTORIES`, `_SIGN_TESTS`, `_TRAP_TESTS`, `_TO_WORD`), and the harness and `tools/`
  have no if/elif dispatch chains (only the guard runs above).
- **Why the constants are lookups now.** `port/python/spimulator_py/opcodes.py` exposes `TOKEN`
  and `KIND` as `dict[str, int]`, and the code reads them about 290 times as `TOK["…"]` /
  `KIND["…"]`. The type checker cannot see those keys, so a misspelled name fails only at run time.
- **The dotted constants exist** (since 2026-11-11, `tasks/archive/2026/11/11/opcodes-x-macro-redesign.md`).
  `tools/gen_opcodes.py` generates `port/python/spimulator_py/opcode_table.py` from
  `include/opcodes.tsv`, as it does every port's table. That module holds `class Tok(enum.IntEnum)`,
  every keyword token named like the C token without `TOK_` (`Tok.LI_PSEUDO_OP`), and
  `class OpType(enum.IntEnum)`, the operand-shape tags. `opcodes.py` builds `TOKEN` and `KIND` from
  them. The image build and a meson test check that the committed module is current
  (`tasks/reference/opcode-table-generator.md`).
- **Some constants are local.** The REPL command numbers in `spim.py` are
  `_EXIT, _READ, … = range(1, 8)`. Under the standard they are an `enum.IntEnum` whatever is
  decided below.

## Decisions

- **Opcode constants: a generated, committed module of `IntEnum` classes** (William Emerison Six
  <billsix@gmail.com>, 2026-11-11, option (b) of two). It is generated as the Haskell and Rust
  ports' tables are, with a check that regenerating changes nothing; convert all ~290
  `TOK["…"]` / `KIND["…"]` lookups so the type checker checks every name. The other option was to keep
  reading `opcodes.h` at import and expose the tables as attribute namespaces (`case Tok.X:`), which
  would have changed only the chains' comparisons. It was not chosen because it leaves the names
  unchecked, where the generated module matches the other two ports and the standard's preference
  for enums over strings.

## Plan

1. Convert the four `value` chains and `ast_nodes.py:136`, each with a `case _`. Make the REPL
   commands an `IntEnum`. Run the port-python suite.
2. Convert the ~290 `TOK["…"]` / `KIND["…"]` lookups to `Tok.…` / `OpType.…`, one module per commit.
   The generated module and its check already exist (Context). `TOKEN` and `KIND` in `opcodes.py`
   can go once nothing reads them.
3. Convert the `lookup` chains, one file per commit. Check that `ruff`, `ty` and
   `check_local_annotations.py` are clean on each, then run `meson test --suite port-python` and
   the whole-tree comparison (`port/harness/compare_all_programs.py`). Behavior must not change;
   the C engine is the standard.
4. Convert the guard runs (`data/guard-runs.txt`): the seven dispatch runs to `match` with the
   chains of step 3, and the other 35 to `if`/`elif`/`else` chains whose `else` holds the code
   that followed the run. Keep a single guard, and checks that can each fire, as separate `if`s.
   Do not enable ruff's `RET505`/`RET506`, which contradict the rule.
5. Record in the done-record each chain left as an `if`, with its reason. At archive, `git rm`
   `tasks/adhoc/python-match-statements/` (one-shot).
