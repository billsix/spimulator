# `cop2` and `jalr.hb` assembled to nothing, silently

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `dispatch-follow-ups`
**Found by:** `tasks/archive/2026/11/11/c-switch-and-else-if-chains.md`, while converting `parse_j`'s
chains (`src/parser.c`) to `switch` statements

## Outcome

`parse_j` handled `j`, `jr`, `jal` and `jalr`. Two more opcodes reached it, and for both it emitted
nothing and reported no error:
- `cop2`, whose row has type `J_TYPE_INST`;
- `jalr.hb`, an `RD_RS_INST` row, which `parse_asm_code` also sends to `parse_j`.

Fixed in all four engines (C, and the Python, Rust and Haskell ports):
- **`jalr.hb` assembles to its own opcode** in both register forms. The encodings match GNU `as`:
  `jalr.hb $t0` → `0x0100fc09`, `jalr.hb $s1, $t0` → `0x01008c09`. With a label it is a parse
  error, "jalr.hb needs a register operand".
- **`cop2` is a parse error:** "cop2 is not supported: spim has no coprocessor 2", exit 2.
- **`jr.hb` and `jalr.hb` now execute.** This was found on the way. `run_spim` had no case for
  either, so running one printed "Unknown instruction type". `jr.hb` assembled correctly through
  its own `RS_INST` path, but couldn't run. Both now run as `jr`/`jalr`: the simulator has no
  pipeline hazards for a barrier to clear.
- `parse_j`'s switch defaults are now fatal, since every opcode that reaches them has a case.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. `cop2` is rejected, not assembled: spim has no coprocessor 2, and a teaching simulator should
   say it doesn't support an instruction.
2. `jalr.hb` assembles as GNU `as` does.

## Verification

- New regression tests, which run in the C suite and in every port suite:
  - `hazard_barrier` (`tests/tt.hazard_barrier.s`): calls through both `jalr.hb` forms, returns
    through `jr.hb`, and checks the three encodings in the `disasm` listing.
  - `cop2_rejected` (`tests/tt.cop2.s`): exit 2, with the message.
- C regression 38/38 (gcc and clang `-Werror`). `port-python`, `port-rust` and `port-haskell`
  (74/74, including the annotation check) all pass. `hlint`'s advisory count is unchanged.
- Oracle: GNU binutils for MIPS (`mips64-linux-gnu-as -mabi=32 -mips32r2 -EB`, then
  `mips64-linux-gnu-objdump -d`).

## Mistakes made and fixed within the task

- The Haskell `jalr` helper was first typed `IO Bool`, but `jump` returns `IO Next`. GHC rejected
  it, so the first port-suite run failed every Haskell test. The type was fixed, and the Haskell
  suite was rerun: 74/74.
- A first Python port run showed 34 demo failures. That was the scratch build: only the
  `spimulator` target had been compiled, so the demos' C binaries were missing. With every target
  built, all pass.
