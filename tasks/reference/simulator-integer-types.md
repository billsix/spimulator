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
| `reg_word` | `int32_t` | a register / word, **signed** view |
| `u_reg_word` | `uint32_t` | the **unsigned** view of the same bits |
| `mem_word` | `int32_t` | a memory word |
| `mem_addr` | `uint32_t` | an address (also the PC) |

Instruction fields are fixed-width too (`instruction.h`): `int16_t` immediate, `uint8_t` register
numbers. The MMIO control registers `recv_control`/`trans_control` (`src/memory.c`) are `uint32_t`
(they are unsigned status registers).

## The rule: signed↔unsigned conversions are the ALU semantics — don't "clean them away"

A MIPS instruction reinterprets the same 32 bits as signed or unsigned depending on the op (`add` vs
`addu`, `sra` vs `srl`, `slt` vs `sltu`, sign-extended branch offsets, effective-address wraparound).
In C that shows up as conversions between `reg_word` and `u_reg_word`/`mem_addr`. **These are correct,
intended behavior, not bugs.** The core makes each such reinterpretation an **explicit cast** to the
target type — e.g. `(u_reg_word)gpr[BASE(instruction)] + (u_reg_word)IOFFSET(instruction)`,
`PC + (mem_addr)BRANCH_OFFSET(instruction)`, `gpr[RD] = (reg_word)(<unsigned result>)`. The cast is a
no-op value-wise; it documents intent and keeps the gate (below) quiet. **Do not remove these casts to
"tidy" run.c** — the compiler will immediately flag the bare conversion again, and removing one that
was masking a genuine width/sign change would be a real bug.

## The gate

The `spimulator` core target is built `-Wconversion -Wsign-conversion -Werror` (via `-Dcore_werror`,
default on, in `meson.build`), under **both** the image's clang and a host gcc. The teaching code under
`examples/` is deliberately NOT gated (see `tasks/reference/example-c-teaching-style.md`).

Staying clean when you touch the core:
- **Cast each signed/unsigned reinterpretation to the exact target type** (`reg_word`/`u_reg_word`/
  `mem_addr`/`size_t`), the same way existing code does.
- **A conversion inside a macro body must be fixed in the macro, not at the call site** — a call-site
  cast cannot reach inside the expansion. The ones already handled this way: `BRANCH_OFFSET` and
  `sign_ex` (`instruction.h`/`spim.h`), `CP0_ExCode`, `FCC`, `SET_FCC` (`registers.h`), `ROUND_UP`/
  `ROUND_DOWN` (`spim.h`).
- **gcc is the stricter of the two** — it flags compound assignments (`reg |= unsigned_mask`) that clang
  passes; write CP0/FCC register bit-twiddling as `reg = (reg_word)((u_reg_word)reg OP mask)`. Build
  under both before declaring done.
- To find conversions: build with the flags and read the diagnostics (each names the from→to types);
  `clang -Xclang -ast-dump` / `clang-query` show every subexpression's type and the implicit casts.
  Tooling snapshot: `tasks/adhoc/integer-conversion-hardening/` (`discover.sh`, `analyze.py`).

## Not yet covered

- **Big-endian paths** (`#ifdef SPIM_BIGENDIAN` in `memory.c`) are not compiled on the little-endian
  host, so their conversions are untouched — `tasks/big-endian-conversion-hardening.md` (blocked).
- A pre-existing `scanner.c` `(int)(len + 16)` narrowing — `tasks/scanner-xmalloc-int-narrowing.md`.

## Cross-links

- `tasks/archive/2026/10/16/integer-conversion-hardening.md` — the work record: full per-class
  breakdown, how ~340 sites were resolved, verification.
- `github.com/billsix/runClaudeInContainer:tasks/reference/c-coding-standard.md` — the integer-type
  rules; its modernization-sweep sibling — when NOT to apply them.
