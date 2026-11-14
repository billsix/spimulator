# Spell out the abbreviated names in the Haskell port (`port/haskell/`)

**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)
**Done:** 2026-11-14, on branch `spelled-out-names-c-core` (commit `b24dcd3`)
**Depended on:** `tasks/archive/2026/11/14/spelled-out-names-c-core.md`

## Summary

The Haskell port's own names were spelled out to match the C core's, in Haskell's camelCase and
CamelCase: 441 names and 4,680 tokens. `gpr` became `generalPurposeRegisters`, `ImmExpr` became
`ImmediateExpression`, and `ssRest` became `scanStateRest`.

- The build is clean under `-Wall -Werror`.
- ormolu was applied.
- hlint's advisory count is unchanged at 47.
- All 88 port-haskell meson tests pass, including the parity runs against the C engine and
  `haskell-annotations`.

## Request (William Emerison Six <billsix@gmail.com>, 2026-11-14)

"Once that's done for the C task, I want a follow up task for the python port, haskell port, and
rust port to do the same." The go-ahead to run the ports with no stop for review: "you should
be able to start on the ports without asking me anything".

## How

- The shared tool is `tasks/adhoc/spelled-out-names-python-port/port_names.py` (see
  `tasks/archive/2026/11/14/spelled-out-names-python-port.md`).
- **The port's own names** are every identifier of `src/` and `tests/` except:
  - the names the imported libraries export (`haskell_external.sh` runs ghci's `:browse` on
    each imported module, into `data/haskell.external.txt`);
  - the keywords;
  - the module names, which are file names;
  - the generated `Opcodes.hs` and `OpcodeTypes.hs`.
- Names of one or two letters stay (pattern and tight-scope variables), except the register
  fields (`rs`, `rt`, `rd`, `fs`, `ft`, `fd`, `fr`, `cc`, `pc`).
- A lowercase one-word name is recased to camelCase (`epc` became `exceptionProgramCounter`),
  primes are kept (`cur'` became `current'`), and familiar acronyms stay upper case
  (`instructionEncodeIO`).
- `port/haskell/tools/` (the annotation checker, written against the GHC API) was left as it is.

## Port-only decisions (`data/haskell.manual.tsv`)

- **Record prefixes** that the glossary would read differently:

  | Prefix | Record | Spelled as |
  | --- | --- | --- |
  | `ss` | `ScanState`, not C's string stream | `scanState` |
  | `ie` | `ImmExpr`, not the interrupt-enable bit | `immediateExpression` |
  | `in` | `Instruction` fields | `instruction` |
  | `lbl` | `Label` | `label` |
  | `opt` | the options | `option` |
  | `Ev` | assembler events | `Event` |
  | `V` | token values | `Value` |

  The register file's `regHI`/`regLO`/`regPC` became `registerMultiplyDivideHigh`,
  `registerMultiplyDivideLow` and `registerProgramCounter` (C decision 2).
- **Singular where the C name is plural:** `getGpr`/`setGpr` (one register) became
  `getGeneralPurposeRegister`/`setGeneralPurposeRegister`. `DestGpr`/`DestFpr` became
  `DestinationGeneralPurposeRegister`/`DestinationFloatingPointRegister`.
- **The `-Wall -Werror` build found these collisions:**
  - `len` became `byteLength`, and `err` became `errorOccurred`, because `length` and `error`
    would shadow Prelude.
  - `breakInst` (the cached break instruction) became `cachedBreakInstruction`.
  - The effective-address action `address` became `effectiveAddress`, freeing `address` for the
    old `addr` locals.
  - `copreg` became `coprocessorOperand`, since `coprocessorRegister` is a function.
- **`Output.hs`'s `fd`** stays a POSIX file descriptor; everywhere else `fd` is the
  floating-point destination field.
- **Kept:**
  - the R/I/J format letters (`iType`, `emitR`);
  - the grammar shapes (`parseB2`, `parseR3`);
  - the single/double helper suffixes (`binaryS`, `setS`);
  - English-word names (`atLine`, `inSlot`, `dataInKernel`);
  - `writeFd` and the numeric helpers.

## Follow-ups

None for this port. The Rust task is the last of the three; its archive removes
`tasks/adhoc/spelled-out-names-python-port/` and `tasks/adhoc/spelled-out-names-c-core/`.
