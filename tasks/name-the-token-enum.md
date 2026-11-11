# Name the token enum, so switches on a token are checked by the compiler

**Status:** proposed — needs go-ahead
**Priority:** 7
**Difficulty:** 5
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Split out of:** `tasks/archive/2026/11/11/c-switch-and-else-if-chains.md` (its decision 4)

## BLUF

`include/tokens.h` declares the token numbers as an anonymous `enum : int32_t`, and opcodes are
passed as `int` and stored as `int16_t` (`mips_instruction.opcode`, `include/instruction.h`). So a
`switch` on an opcode is a `switch` on an integer: neither `-Wswitch` nor `-Wswitch-enum` can check
it. Examples are `lookup_classification`'s 60 cases, `r_cond_type_inst`'s 32, and
`explain_before`/`explain_after`. Give the enum a name and the subjects that type. Done means the
generator emits a named type, the switches the task chooses take it, the build is clean under
`-Werror -Wswitch-enum`, and `make image` passes.

## Context

- This is the C counterpart of the Python port's generated `Tok` `IntEnum`.
  `tools/gen_opcodes.py` writes `tokens.h`; change its emitter there, never the generated file
  (`CLAUDE.md`, "Generated sources are committed").
- **The catch is `-Wswitch-enum` itself.** A `switch` on a named token type must then name every
  member, roughly 500 tokens, or opt out at the site. Most opcode switches deliberately handle a few
  opcodes. So naming the type is only half the work; each switch needs a decision: keep it on `int`,
  or list members and opt out with the reason. The ones that gain most are the switches meant to
  cover a closed subset (`r_cond_type_inst`'s compares), which a per-type subset enum could cover.
- **The storage is `int16_t`** (`SET_OPCODE` casts). Retyping the field to the enum (`: int32_t`
  underlying) changes the struct's size. The decision is per field.
- **The ports** have their own token types already (Python `Tok`, the Rust and Haskell generated
  modules). This is a C-only change.

## Open questions

1. **Is the check worth the churn?** (a) Name the type and convert only the switches that cover a
   closed subset, or (b) leave it, since the fatal `default`s already catch a bad value at run time.
   Recommend (b) unless a subset switch turns out to have missed members again. (It did once:
   `r_cond_type_inst` and the paired-single compares, `tasks/paired-single-compare-encoding.md`.)
