# The simulator's integer-type model and the `-Wconversion` gate

**Reference document.** Read before editing the integer arithmetic in the simulator core
(`src/run.c` especially, plus `instruction.c`/`memory.c`/`syscall.c`), before adding an instruction, or
before changing the warning flags. States how the core's integer types work *now* and why the explicit
casts are there. Durable; update in place. (Written 2026-10-16 — William Emerison Six
<billsix@gmail.com> — harvested from `tasks/archive/2026/10/16/integer-conversion-hardening.md`.)

## The machine-word types

A MIPS register/word is 32 bits, modeled with fixed-width domain typedefs (`include/registers.h`,
`include/memory.h`, `include/spim.h`):

| typedef | underlying | role |
| --- | --- | --- |
| `register_word` | `int32_t` | a register / word, **signed** view |
| `unsigned_register_word` | `uint32_t` | the **unsigned** view of the same bits |
| `memory_word` | `int32_t` | a memory word |
| `memory_address` | `uint32_t` | an address (also the PC) |

Instruction fields are fixed-width too (`instruction.h`): `int16_t` immediate, `uint8_t` register
numbers. The MMIO control registers `receiver_control`/`transmitter_control` (`src/memory.c`) are `uint32_t`
(they are unsigned status registers).

## The rule: signed↔unsigned conversions are the ALU semantics — don't "clean them away"

A MIPS instruction reinterprets the same 32 bits as signed or unsigned depending on the op (`add` vs
`addu`, `sra` vs `srl`, `slt` vs `sltu`, sign-extended branch offsets, effective-address wraparound).
In C that shows up as conversions between `register_word` and `unsigned_register_word`/`memory_address`. **These are correct,
intended behavior, not bugs.** The core makes each such reinterpretation an **explicit cast** to the
target type — e.g. `(unsigned_register_word)general_purpose_registers[BASE(instruction)] + (unsigned_register_word)IMMEDIATE_OFFSET(instruction)`,
`PC + (memory_address)BRANCH_OFFSET(instruction)`, `general_purpose_registers[RD] = (register_word)(<unsigned result>)`. The cast is a
no-op value-wise; it documents intent and keeps the gate (below) quiet. **Do not remove these casts to
"tidy" run.c** — the compiler will immediately flag the bare conversion again, and removing one that
was masking a genuine width/sign change would be a real bug.

## The gate

The `spimulator` core target is built `-Wconversion -Wsign-conversion -Werror` (via `-Dcore_werror`,
default on, in `meson.build`). The image build gates it under clang only; gcc (stricter, below) is
checked by hand with a host build, and the core is clean under it too. The teaching code under
`examples/` is deliberately NOT gated (see `tasks/reference/example-c-teaching-style.md`).

Staying clean when you touch the core:
- **Cast each signed/unsigned reinterpretation to the exact target type** (`register_word`/`unsigned_register_word`/
  `memory_address`/`size_t`), the same way existing code does.
- **A conversion inside a macro body must be fixed in the macro, not at the call site** — a call-site
  cast cannot reach inside the expansion. The ones already handled this way: `BRANCH_OFFSET` and
  `sign_ex` (`instruction.h`/`spim.h`), `coprocessor_0_exception_code`, `FCC`, `SET_FCC` (`registers.h`), `ROUND_UP`/
  `ROUND_DOWN` (`spim.h`).
- **gcc is the stricter of the two** — it flags compound assignments (`reg |= unsigned_mask`) that clang
  passes; write CP0/FCC register bit-twiddling as `reg = (register_word)((unsigned_register_word)reg OP mask)`. Build
  under both before declaring done.
- To find conversions: build with the flags and read the diagnostics (each names the from→to types);
  `clang -Xclang -ast-dump` / `clang-query` show every subexpression's type and the implicit casts.
  The one-shot scan used for the hardening (`discover.sh`, `analyze.py`) is in git history:
  `git log --all -- tasks/adhoc/integer-conversion-hardening/`. The `-Wconversion` gate replaced it.

## Not yet covered

- **Big-endian paths** (`#ifdef SPIM_BIGENDIAN` in `memory.c`) are not compiled on the little-endian
  host, so their conversions are untouched — `tasks/big-endian-conversion-hardening.md` (blocked).
- A pre-existing `scanner.c` `(int)(len + 16)` narrowing — `tasks/archive/2026/11/11/scanner-xmalloc-int-narrowing.md`.

## Cross-links

- `tasks/archive/2026/10/16/integer-conversion-hardening.md` — the work record: full per-class
  breakdown, how ~340 sites were resolved, verification.
- `github.com/billsix/runClaudeInContainer:tasks/reference/c-coding-standard.md` — the integer-type
  rules; its modernization-sweep sibling — when NOT to apply them.
