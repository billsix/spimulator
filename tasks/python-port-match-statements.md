# Use `match` for the Python port's if/elif dispatch chains

**Status:** ready — decision made, implementation not started
**Priority:** 6
**Difficulty:** 4
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)

## BLUF

Fifteen if/elif chains in the Python port pick a branch by comparing one value against a list of
constants, the shape a C `switch` has. Rewrite each as a `match` with a `case _` default, as the
Python coding standard asks. Most of these chains compare against string-keyed dict lookups
(`TOK["LI_PSEUDO_OP"]`) or bare names, which a `case` cannot spell, so the opcode constants must
first become dotted names: members of `IntEnum` classes in a module generated from
`include/opcodes.h` (Decisions). Done means each chain is a `match`
or is listed with the reason it stays an `if`, and the port suites still pass.

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
- **No other code is a candidate.** The executor already dispatches through dict tables
  (`run.py` `_FACTORIES`, `_SIGN_TESTS`, `_TRAP_TESTS`, `_TO_WORD`), and the harness and `tools/`
  have no qualifying chains.
- **Why the constants are lookups now.** `port/python/spimulator_py/opcodes.py` reads
  `include/opcodes.h` at import, so the port can never disagree with C. It exposes `TOKEN` and
  `KIND` as `dict[str, int]`, and the code reads them about 290 times as `TOK["…"]` /
  `KIND["…"]`. The type checker cannot see those keys, so a misspelled name fails only at run time.
- **The other two ports commit a generated module.** Haskell has `port/haskell/src/Opcodes.hs`,
  from `port/haskell/tools/GenOpcodes.hs`. Rust has `port/rust/src/opcodes_table.rs`, from
  `port/rust/tools/gen_opcodes.sh`. Neither port's suite checks that its generated file still
  matches `opcodes.h`.
- **Some constants are local.** The REPL command numbers in `spim.py` are
  `_EXIT, _READ, … = range(1, 8)`. Under the standard they are an `enum.IntEnum` whatever is
  decided below.

## Decisions

- **Opcode constants: a generated, committed module of `IntEnum` classes** (William Emerison Six
  <billsix@gmail.com>, 2026-11-11, option (b) of two). Generate it from `include/opcodes.h` as the
  Haskell and Rust ports do, add a check that regenerating changes nothing, and convert all ~290
  `TOK["…"]` / `KIND["…"]` lookups so the type checker checks every name. The other option was to keep
  reading `opcodes.h` at import and expose the tables as attribute namespaces (`case Tok.X:`), which
  would have changed only the chains' comparisons. It was not chosen because it leaves the names
  unchecked, where the generated module matches the other two ports and the standard's preference
  for enums over strings.

## Plan

1. Convert the four `value` chains and `ast_nodes.py:136`, each with a `case _`. Make the REPL
   commands an `IntEnum`. Run the port-python suite.
2. Write the generator (in `port/python/tools/`) and commit the generated module. It must give the same
   token numbers as `include/tokens.h`, which `-print-ast` prints. Add a staleness check (regenerate,
   expect no diff) to `port/meson.build`. Then convert the ~290 lookups, one module per commit.
3. Convert the `lookup` chains, one file per commit. Check that `ruff`, `ty` and
   `check_local_annotations.py` are clean on each, then run `meson test --suite port-python` and
   the whole-tree comparison (`port/harness/compare_all_programs.py`). Behavior must not change;
   the C engine is the standard.
4. Record in the done-record each chain left as an `if`, with its reason. At archive, `git rm`
   `tasks/adhoc/python-match-statements/` (one-shot).
