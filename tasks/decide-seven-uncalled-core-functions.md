# Decide what happens to seven core functions that nothing calls

**Status:** proposed — needs the maintainer's decision per function (open questions below)
**Priority:** 7 (dead code costs reading time, nothing else)
**Difficulty:** 2 (a removal per function, its header declaration, and the port's mirror if it has one)
**Created:** 2026-11-25 (William Emerison Six <billsix@gmail.com>, answering "do you want a task in spimulator to decide what happens to the seven uncalled functions?": "yes").
**Source:** `tasks/review/2026/11/10/repo-review.md` §4.9 (it lists six of the seven under their names of that date); found again by `tasks/archive/2026/11/25/apply-naming-and-scope-rules-per-language.md` ("Found on the way"), which added `print_undefined_symbols`.

## BLUF

Seven functions in the simulator core are defined in one `src/*.c` file, declared in its header, and called from nowhere. This task decides, per function, whether to remove it or keep it for a use the maintainer has in mind, then does that. Done means each of the seven is either gone (definition, declaration, and any port function that mirrors it) or carries a comment saying what it is kept for, with `make image` green.

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

**The same review section lists other dead items** (`scanner_next`, unused macros, `src/dump-opcodes.c`). Check which of those are still present before starting; they are outside this task unless the maintainer widens it.

## Plan

- [ ] Get the answers below.
- [ ] Per function to remove: delete the definition and the declaration, and the mirror in each port; rebuild.
- [ ] Per function to keep: a comment at the definition saying what it is kept for.
- [ ] Gate: `make image` (default flags).

## Open questions

1. Remove all seven (the agent's recommendation, since git keeps them), or keep some?
2. If `.lcomm` support is planned, should `lcomm_directive` stay as its starting point?
3. Should this task also clear the other dead items the 2026-11-10 review lists in §4.9 that are still present?
