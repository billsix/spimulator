# Name the token enum, so switches on a token are checked by the compiler — declined

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Closed:** 2026-11-11, declined, with no work done
**Split out of:** `tasks/archive/2026/11/11/c-switch-and-else-if-chains.md` (its decision 4)

## What was proposed

`include/tokens.h` declares the token numbers as an anonymous `enum : int32_t`, and opcodes are
passed as `int` and stored as `int16_t`. So no compiler warning can check a `switch` on an opcode.
The proposal was to have `tools/gen_opcodes.py` emit a named type and give switch subjects that
type.

## Decision (William Emerison Six <billsix@gmail.com>, 2026-11-11)

Declined. The fatal `default`s that task added already catch a bad token at run time. With the
core built under `-Wswitch-enum`, a switch on a named token type would have to list all ~500
tokens or opt out at the site. That is churn without a matching gain. The one subset switch that
had missed members, `r_cond_type_inst` and the paired-single compares, is handled by removing those
rows (`tasks/paired-single-compare-encoding.md`). Revisit if a subset switch misses members again.
