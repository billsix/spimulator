# Removed seven uncalled core functions and the other dead code the 2026-11-10 review listed

**Completed:** 2026-11-26
**Created:** 2026-11-25 (William Emerison Six <billsix@gmail.com>, answering "do you want a task in spimulator to decide what happens to the seven uncalled functions?": "yes").
**Source:** `tasks/review/2026/11/10/repo-review.md` §4.9; found again by `tasks/archive/2026/11/25/apply-naming-and-scope-rules-per-language.md` ("Found on the way"), which added `print_undefined_symbols`.

## Decisions

William Emerison Six <billsix@gmail.com>, 2026-11-26:

1. Remove all seven functions.
2. `lcomm_directive` goes with the rest. `.lcomm NAME SIZE` is an assembler directive that reserves SIZE zeroed bytes under a file-local label; the simulator reports it as "Directive not yet supported", and no support is planned. `.comm` and `.space` cover the same need, and git keeps the function.
3. Also remove the other dead items of the review's §4.9 that are still present.

## What was removed

Names are the current ones; the review's name of 2026-11-10 follows in parentheses where the 2026-11-14 spelled-out-names pass changed it.

**The seven functions**, each with its header declaration:

| Function | File |
|---|---|
| `assembler_fire_align` (`asm_fire_align`) | `src/assembler-event.c` |
| `assembler_fire_segment_change` (`asm_fire_seg_change`) | `src/assembler-event.c` |
| `ast_make_error` | `src/ast.c` |
| `extern_directive` | `src/data.c` |
| `lcomm_directive` | `src/data.c` |
| `initialize_stack`, with `MAX_ARGUMENTS`, which only it used | `src/spim-utils.c` |
| `print_undefined_symbols` | `src/symbol-table.c` |

**The other §4.9 items:**

- `scanner_next` (`src/scanner.c`).
- `DIRECT_MAPPED`, `TWO_WAY_SET` (`include/spim.h`); `TEXT_CHUNK_SIZE` (`include/memory.h`).
- `multiply_divide_high_present`, `multiply_divide_low_present` (`HI_present`, `LO_present`; `src/memory.c`).
- `next_program_counter` (`nPC`): the definition in `src/memory.c` and the `extern` in `include/registers.h`.
- The code under `TEST_ASM`, a macro nothing defines: the call in `src/run.c`, and `test_assembly` and `compare_instructions` (`inst_cmp`) in `src/instruction.c`.
- The three `#ifdef MIPS1` arms (`include/spim.h`, two in `src/run.c`); the MIPS32 arm of each stays. `rfe` raises the reserved-instruction exception, as it did.
- The else-arm of `if (shift_amount >= 0 && shift_amount < 32)` in `sllv`, `srav` and `srlv` (`src/run.c`), where `shift_amount` is a register value masked with `0x1f`.
- `address <= MEMORY_MAPPED_IO_TOP` in `bad_memory_read` and `bad_memory_write` (`src/memory.c`): `address` is a 32-bit unsigned value and the constant is `0xffffffff`. A comment at each site names the constant, which stays in `include/memory.h` as part of the memory map.

**One removal the compiler asked for.** With `extern_directive` and `lcomm_directive` gone, `next_global_pointer_item_address` in `src/data.c` was written and never read, and the `-Werror` build stopped on it. It went, and so did its mirrors: the field in `port/rust/src/machine.rs` with its write in `port/rust/src/data.rs`, and `_next_global_pointer_item_address` in `port/python/spimulator_py/data.py`. The Haskell port had none.

## Already gone, or left

- `src/dump-opcodes.c` was already deleted.
- **The ports mirrored none of the seven functions.** `git grep` for each name in `port/` finds only the `.extern` and `.lcomm` token rows of the generated opcode tables.
- **Left: the same test in `sll`, `sra` and `srl`.** There `shift_amount` is `SHIFT_AMOUNT(instruction)`, a field of the decoded instruction; the review named only the three masked sites.
- **Left: the event kinds `ASSEMBLER_EVENT_ALIGN` and `ASSEMBLER_EVENT_SEGMENT_CHANGE`.** Nothing fires them now. The `-explain` observer in `src/spim.c` still has a case for each, and they stay so the event type keeps describing what an assembler can report.
- **Left, on the agent's judgment: the `qsort` calls on `name_tbl` and `i_opcode_tbl`** in `src/instruction.c`. The review marks them a lead; they are no-ops only while `tokens.h` numbers its tokens in `opcodes.h` row order.
- `.lcomm` and `.extern` keep their tokens and their parser paths; only the two unused functions went.

## Verification

`make image` with the default flags, after the last change: the build under `-Werror`, 385 tests, then 54 under UBSan and 54 under ASan, all passing. `git grep` for each removed name in `src`, `include` and `port` finds nothing.
