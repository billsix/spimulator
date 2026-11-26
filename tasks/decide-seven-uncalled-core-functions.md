# Decide what happens to seven core functions that nothing calls

**Status:** decided 2026-11-26, ready to implement — waiting for the maintainer's go-ahead
**Priority:** 7 (dead code costs reading time, nothing else)
**Difficulty:** 3 (a removal per item, its header declaration, and the port's mirror if it has one; about twenty items)
**Created:** 2026-11-25 (William Emerison Six <billsix@gmail.com>, answering "do you want a task in spimulator to decide what happens to the seven uncalled functions?": "yes").
**Source:** `tasks/review/2026/11/10/repo-review.md` §4.9 (it lists six of the seven under their names of that date); found again by `tasks/archive/2026/11/25/apply-naming-and-scope-rules-per-language.md` ("Found on the way"), which added `print_undefined_symbols`.

## BLUF

Seven functions in the simulator core are defined in one `src/*.c` file, declared in its header, and called from nowhere. The maintainer decided on 2026-11-26 to remove all seven, and to remove in the same task the other dead items that the 2026-11-10 review lists in §4.9 and that are still present. Done means each item is gone (definition, declaration, and any port function that mirrors it) or is recorded as already gone or left with the reason, with `make image` green.

## Context

**The seven** (checked 2026-11-25 with `git grep -w NAME -- src include tests examples port/harness`, which finds each only at its definition and its declaration):

| Function | Defined in | Came in with | What it does |
|---|---|---|---|
| `assembler_fire_align` | `src/assembler-event.c` | the assembler-event observer (named `asm_fire_align` until the 2026-11-14 spelled-out-names pass) | notifies the observer of an alignment |
| `assembler_fire_segment_change` | `src/assembler-event.c` | same (`asm_fire_seg_change`) | notifies the observer of a segment change |
| `ast_make_error` | `src/ast.c` | `b873eee2` (2026-05-21, the AST work) | builds an error node |
| `extern_directive` | `src/data.c` | upstream SPIM (1998) | processes `.extern NAME SIZE` |
| `lcomm_directive` | `src/data.c` | upstream SPIM (1998) | processes `.lcomm NAME SIZE` |
| `initialize_stack` | `src/spim-utils.c` | upstream SPIM (2010, "Factored out common code") | builds the stack from a command-line string |
| `print_undefined_symbols` | `src/symbol-table.c` | upstream SPIM (1998) | prints every undefined symbol |

They are not `static`, so `-Wunused-function` does not report them, and the `-Werror` build stays green.

**Read first:** `CLAUDE.md` ("Conventions": the core follows the shared C standard; a C change must be followed in every port). Check each port for a function that mirrors one of these (`git grep -n -i "extern_directive\|externDirective\|lcomm" -- port`); a mirror that is also uncalled goes with its original, so the ports keep matching the C engine function for function.

**What the directives do today.** The parser handles `.extern` and `.comm` through `parse_directive_extern` and `parse_directive_comm` and their `emit_*` counterparts; `.lcomm` is reported as "Directive not yet supported" (the 2026-11-10 review, §4.9 and its directive table). So `extern_directive` and `lcomm_directive` are leftovers of the pre-AST parser, unless `.lcomm` support is planned.

**The other dead items of §4.9, now in scope.** The review names them as of 2026-11-10, before the 2026-11-14 spelled-out-names pass, so search for each under its old name and its likely new one, and record which were already gone:

- `scanner_next` (uncalled function);
- unused macros and declarations: `DIRECT_MAPPED`, `TWO_WAY_SET` (`include/spim.h`), `HI_present`, `LO_present` (`src/memory.c`), `nPC` (`include/registers.h`), `TEXT_CHUNK_SIZE`;
- code under a macro that is never defined: `TEST_ASM` (`test_assembly`, `inst_cmp`) and `MIPS1`;
- `src/dump-opcodes.c`, which is not in `meson.build` and does not compile;
- branches that cannot be taken: the `shamt` else-branches after `& 0x1f` in `src/run.c`, and `addr <= MM_IO_TOP` on a `uint32`.

**Left out on the agent's judgment:** the `qsort` calls on `name_tbl` and `i_opcode_tbl` in `src/instruction.c`. The review marks them a lead, and they are no-ops only while `tokens.h` numbers its tokens in `opcodes.h` row order; removing them would turn a future reordering into a wrong lookup.

## Plan

- [ ] Remove the seven functions: definition, declaration, and the mirror in each port; rebuild.
- [ ] Find each §4.9 item above; remove the ones still present, record the ones already gone.
- [ ] For the two impossible branches, remove the dead arm and keep the live one unchanged.
- [ ] Gate: `make image` (default flags).

## Decisions

William Emerison Six <billsix@gmail.com>, 2026-11-26:

1. Remove all seven functions.
2. `lcomm_directive` goes with the rest. `.lcomm NAME SIZE` is an assembler directive that reserves SIZE zeroed bytes under a file-local label; the simulator reports it as "Directive not yet supported", and no support is planned. `.comm` and `.space` cover the same need, and git keeps the function if `.lcomm` is ever wanted.
3. Also remove the other dead items of the review's §4.9 that are still present (listed in Context).

## Open questions

None.
