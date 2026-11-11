# Use `match` for the Python port's dispatch chains, and `elif` for its guard runs

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** spimulator `python-port-match-statements`, off `opcodes-generator` (it needs the generated
`Tok` and `OpType` enums)

## Outcome

The Python port follows the Python coding standard's two rules on conditionals ("Prefer `match` +
`case _`" and "Mutually exclusive branches are one if/elif/else chain"):
- **21 dispatch sites are `match` statements, each with a `case _`:**
  - 14 if/elif chains that compared one value;
  - 7 runs of separate early-exit `if`s on one value, including `instruction.py`'s two
    operand-shape runs of 14 and 24 `if`s.
- **38 runs of separate early-exit `if`s are now single `if`/`elif`/`else` chains,** with the code
  that followed in the `else`. That is 37 runs by the converter, plus `_parse_imm32` by hand.
- **The ~290 string-keyed lookups (`TOK["X"]`, `KIND["X"]`) are `Tok.X` and `OpType.X`.** Those are
  the `IntEnum`s `tools/gen_opcodes.py` generates into `opcode_table.py`, so the type checker sees
  every token name.
- **The REPL command numbers are `class Command(enum.IntEnum)`.**

Behavior is unchanged: port-python 71 OK, with the whole-tree comparison with the C engine, plus
the regression and examples suites.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

- **Opcode constants: a generated, committed module of `IntEnum` classes** (option (b) of two). It
  was delivered by `tasks/archive/2026/11/11/opcodes-x-macro-redesign.md`, which generates
  `opcode_table.py` for this port as for the others. The rejected option exposed attribute
  namespaces over the import-time parse, which would have left the names unchecked.
- **Guard runs are in scope too.** The maintainer added them to the task: runs of separate `if`s
  that are one decision become one chain, per the standard.

## How, in commit order

All the scripts named here are under `tasks/adhoc/python-port-match-statements/`.
1. **Enum members.** `to_enums.py` converted the literal lookups and the aliases.
   - A lookup by a computed name, `TOK[f"{name}_OPCODE"]`, became `Tok[...]`: an Enum looks
     members up by name, with the same `KeyError`.
   - `instruction.py`'s 26 module-level tag aliases went.
   - `name_collections.py` turned the collections of token-name strings into members:
     `_tokens(...)` calls, `TOK[name] for name in (...)` tuples, and `pseudo_op.py`'s two
     name-to-name tables.
2. **`match`.**
   - `pre_edits.py` prepared the three chains a `case` could not spell. It added the `Command`
     enum, numbered as the C engine's `spim.c` enum (checked); wrote the structural tokens as
     `opcodes.TOK_REG`; and rewrote the escape chain on `chr(escape)` instead of `ord(...)` calls.
   - `to_match.py` then rewrote the source text, using the AST only to find each chain, so comments
     and layout survive.
   - A link that tests something other than the subject kept its test as a guard
     (`case _ if arg in _VALUE_FLAGS and i + 1 >= argc:`).
   - A chain with no `else` got `case _: pass  # any other value: nothing to do, as before`.
3. **Guard runs.** `to_match.py elif` converted the runs, rescanning after each round, because
   converting an inner run can make an outer one no longer a run.
   - Done by hand: `_parse_imm32`, where an outer run's first branch ends in an inner chain.
   - `syscall.py`'s two adjacent `return`-only branches were merged into one test. ruff's SIM114
     flagged them, and it is the standard's rule B (dedupe identical branches).

## Mistakes made and fixed within the task

The first `match` runs had converter bugs, caught by checking that every comment survives and by
ruff:
- comments between a header and its body were dropped;
- a parenthesized multi-line test's `):` line went into the body;
- an `else:` line was left behind;
- the code after a dispatch run was placed one level too shallow;
- a trailing comment on an `else:` line was lost.

Each time the files were restored from the previous commit and the run repeated with the fixed
tool. The final check found every comment of the converted modules still present.

An early `ruff format` over all of `tools/` reformatted `tools/check_asm_canonical.py`, which was not
ruff-formatted before. That was undone, and only files that were already format-clean, or are this
branch's own, were formatted.

## Verification

- At each step: ruff, ty and `check_local_annotations.py` clean; the generated opcode tables are
  unchanged (`tools/gen_opcodes.py --check`).
- port-python 71 OK after each of the three steps. The final step also ran the regression and
  examples suites (the `tools/` scripts it touched are used there).

## Follow-ups

- The scans that found the chains (`match_candidates.py`, `elif_chain_candidates.py`, under
  `tasks/adhoc/python-match-statements/`) are one-shot and leave with this task's archive.
