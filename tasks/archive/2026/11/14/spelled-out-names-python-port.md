# Spell out the abbreviated names in the Python port (`port/python/`)

**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)
**Done:** 2026-11-14, on branch `spelled-out-names-c-core` (commit `7f23a4b`)
**Depended on:** `tasks/archive/2026/11/14/spelled-out-names-c-core.md`

## Summary

The Python port's own names were spelled out to match the C core's, so a student reading the
port beside the C sees the same names in the same places. There were 375 renamed names and 4,662
renamed tokens; `gpr` became `general_purpose_registers` and `inst` became `instruction`. All 86
port-python meson tests pass, and so does the lint gate (`port/python/lint.sh`: ruff, ty, the
annotation check and 38 doctests). The parity runs against the C engine (`compare_engines.py`,
`compare_all_programs.py`, `compare_repl.py`) are among those tests.

## Request (William Emerison Six <billsix@gmail.com>, 2026-11-14)

"Once that's done for the C task, I want a follow up task for the python port, haskell port, and
rust port to do the same." The go-ahead to run the ports straight after the C task, with no
stop for review: "you should be able to start on the ports without asking me anything".

## How

- `tasks/adhoc/spelled-out-names-python-port/port_names.py` (shared by the three port tasks)
  lists the names the port declares, from Python's `ast`.
  - Each name that spells a C name in the port's casing (`gpr`, `CP0_STATUS`) takes the C name's
    new words. Each other name gets the glossary's word spellings.
  - `data/python.manual.tsv` decides the rest, and the result is `data/python.names.tsv`.
- **The renaming** went by `tokenize` token: in code, f-string expressions and docstrings, but
  never in other string literals. Dunder methods and Python's builtins are never renamed.
- **The port-only words:**
  - `cpr` and `ccr` became `coprocessor_registers` and `coprocessor_control_registers` (the CPU
    object's register banks);
  - `CAUSE_EXCCODE` became `CAUSE_EXCEPTION_CODE`;
  - a local `brk` became `breakpoint_instruction`;
  - `_emit_data_ints` became `_emit_data_integers`.

## Exceptions and hand fixes

Spellings that mean two things in the port, and names inside strings, were handled by
`alias_instruction_module.py` (before the token pass) and `fix_python_after_apply.py` (after it).

- **The `instruction` module** is imported as `instruction as instruction_module` in the eight
  files that use it, because the port's locals named `inst` became `instruction`.
  `from .inst import` keeps the `inst` module (`inst.py`).
- **`console.py`'s `fd`** stays `fd`: it is a POSIX file descriptor there. Everywhere else `fd`
  is the floating-point destination field.
- **`cpu` and `CPU`** (the register-file object and its class) keep their names. The glossary's
  `cpu` is the CpU exception code, and `ExcCode.CPU` shares the class's spelling.
- **`ExcCode` became `ExceptionCode`.** Its members were renamed by a targeted pass
  (`ExceptionCode.ADDRESS_ERROR_LOAD  # AdEL: …`), with the manual's mnemonic in the comment.
- **The CPU object's attributes:**
  - `hi`/`lo` are the HI and LO registers, so they became `multiply_divide_high` and
    `multiply_divide_low` (C decision 2), not the generic `high`/`low`;
  - `fpr` holds all 32 floating-point registers, so it is plural: `floating_point_registers`;
  - its `__slots__` strings and the class docstring follow.
- **Quoted forward references** (`"ImmExpr"`) follow their classes.
- **The `Kind` members** print in `-print-ast` and `-print-ast-json`. They were renamed
  (`DIRECTIVE_DATA`), and `printed_name` gives the C engine's printed spelling (`DIR_DATA`), so
  the output is byte-identical to the C engine's.
- **Kept:**
  - the R/I/J format letters (`i_type`, `emit_r`);
  - the grammar shapes (`_parse_b2`, `_parse_r3`);
  - the numeric helpers (`s32`, `u8`, `to_s32`);
  - termios's `iflag`/`cflag`;
  - `PSEUDO_OP`;
  - English-word names (`at_eof`, `in_text`).

## Follow-ups

None for this port. The Haskell and Rust tasks reuse `port_names.py`. The last of them to be
archived removes `tasks/adhoc/spelled-out-names-python-port/` and
`tasks/adhoc/spelled-out-names-c-core/`.
