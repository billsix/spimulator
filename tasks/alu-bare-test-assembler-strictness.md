# Make upstream's ALU torture test `tt.alu.bare.s` run

**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Done:** 2026-11-11
**Source:** tasks/review/2026/11/10/repo-review.md §6.1 (F7)

## Summary

`tests/tt.alu.bare.s` is James Larus's ALU torture test for bare-machine mode, which upstream ran as
`spim -bare -noexception -file tt.alu.bare.s` (`git show 6cb6a08:spim/Makefile`, lines 160–165). It had
never run in the fork: 19 of its lines did not assemble. The test was edited to fit the fork, and it
now runs in the regression suite as `alu_bare` (and as `python-alu_bare`, `haskell-alu_bare`,
`rust-alu_bare` in the port suites), printing `Passed all tests`.

## Decision

The maintainer chose to edit the test rather than loosen the assembler (William Emerison Six
<billsix@gmail.com>, 2026-11-11, option (a) of the two offered). The other option was to make the
assembler truncate out-of-range immediates under `-bare` and to add the MIPS-I coprocessor-3
instructions. It was rejected because the fork's range check is a teaching feature and MIPS32 has no
coprocessor 3.

## What changed in the test

The header of `tests/tt.alu.bare.s` lists the changes. Each removed section is marked by a comment
where it was.
- **Nine immediates written in range.** `addi $4 $0 0xff98` became `-104` (and likewise `0xffba`,
  `0xffdc`, `0xfffe`). `-1` in `ori`, `xori` and `lui` became `0xffff`. Each is the value the
  instruction received under upstream's silent 16-bit truncation, so the test checks what it always
  checked.
- **Coprocessor 0, 2 and 3 sections removed.**
  - The coprocessor-3 tests (`ctc3`/`cfc3`, `mtc3`/`mfc3`, `lwc3`/`swc3`): MIPS32 has no
    coprocessor 3.
  - The `lwc0`/`swc0` test: MIPS32 uses those opcodes for `ll`/`sc`.
  - The coprocessor-2 tests (`ctc2`/`cfc2`, `mtc2`/`mfc2`, `lwc2`/`swc2`): these were not among the
    19 errors, because they assemble. But spimulator has no coprocessor 2, and its instructions
    raise Coprocessor Unusable (`src/run.c`), so the run stopped there with exit 139 (128 + ExcCode
    11). Removing them follows from the same decision.
- Only text-segment code was removed, so the test's hard-coded data addresses (`lui`/`ori 0x1000xxxx`)
  still point at the same strings.

## Other changes

- `tests/run-test.sh` and `meson.build` gained the `alu_bare` case.
- `tests/parity-programs.txt` lists the file, so `ast_parity_all` pins its assembled bytes;
  `tests/golden.ast_parity_all.txt` was regenerated with `tests/gen-parity-golden.sh`, and the change
  only added lines.
- `port/harness/corpus.txt` has an `alu_bare` case.

## Found along the way

- An "immediate value out of range" error names the line after the bad instruction. Recorded as
  `tasks/immediate-range-error-line-number.md` (proposed).
- Exit statuses 134 and 139 seen while debugging were not crashes: they are the fork's 128 + ExcCode
  convention (bus error on an instruction fetch, Coprocessor Unusable).
