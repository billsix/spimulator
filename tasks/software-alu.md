# Task: implement the ALU in C, bit-level, per H&P

## Goal

Replace the host-arithmetic implementations of MIPS arithmetic
operations with a software simulation of the actual gate-level /
bit-level ALU described in Patterson & Hennessy *Computer
Organization and Design*, Chapter 4 ("The Processor: Datapath and
Control"). Instead of `R[rd] = R[rs] + R[rt]`, the simulator does
addition the way real hardware does it: a 32-bit ripple-carry adder
built from 1-bit full-adder slices.

The pedagogical win: students who have just been taught how an ALU
is built from gates can now step through the simulator and see the
same construction they drew on paper produce the same answer.
Optionally, the explain mode can narrate the bit-level activity
(carry chain, overflow detection, ALUop control lines), turning the
simulator into a software model of the datapath, not just an
instruction-level interpreter.

## Why this is a substantial project

A real ALU is not one piece of code — it's a hierarchy:

- **1-bit full adder** (sum and carry-out from A, B, and carry-in
  via XOR/AND/OR).
- **32-bit ripple-carry adder** from 32 1-bit slices chained
  carry-out to carry-in.
- **32-bit subtractor** = adder with B inverted and carry-in = 1
  (two's complement).
- **Overflow detection** = XOR of the top two carries.
- **Logical ops** (AND, OR, XOR, NOR) — bitwise per slice.
- **Shift unit** — barrel shifter (or shift register for simplicity).
- **Multiplier** — H&P walks through three increasingly refined
  designs (32-step shift-and-add into a 64-bit accumulator). The
  third design is what real hardware uses.
- **Divider** — restoring or non-restoring division algorithm,
  also 32 steps.
- **Floating-point** — separate, much bigger. IEEE 754 single
  precision: extract sign/exponent/mantissa, align by shifting
  the smaller-exponent mantissa right, add/subtract mantissas,
  normalize, round. Mul/div on the mantissa with separate
  exponent arithmetic.

Each can be implemented as a function that takes uint32 operands
and a few control signals and returns a uint32 result plus status
flags. Together they replace ~15-20 dispatch handlers in `run.c`.

## Suggested tier-by-tier scope

Don't try to do everything at once. Three tiers:

### Tier 1 — integer arithmetic and logic (most-bang-for-buck)

- `alu_add(a, b, cin)` → `(sum, cout, overflow)` via ripple-carry.
- `alu_sub(a, b)` via add with B inverted, cin=1.
- `alu_and/or/xor/nor(a, b)` — bitwise loop.
- `alu_slt(a, b)` via subtract then check sign bit.
- `alu_sltu(a, b)` via subtract with unsigned overflow as the
  comparator.

These cover ADD, ADDU, ADDI, ADDIU, SUB, SUBU, AND, ANDI, OR, ORI,
XOR, XORI, NOR, SLT, SLTU, SLTI, SLTIU. ~17 opcodes. The single
biggest cluster.

LUI is `alu_or(imm << 16, 0)` essentially — fits here.

Performance: each integer op becomes 32 iterations of a few cheap
ops. Roughly 100× native-speed overhead. For teaching, fine.

### Tier 2 — shifts

- `alu_sll(value, shamt)`, `alu_srl(...)`, `alu_sra(...)` — shift
  register or barrel shifter implementation. Sign-fill for SRA.
- Also `sllv` / `srlv` / `srav` (shamt from register).

### Tier 3 — multiply and divide

- `alu_mult(a, b)` → `(HI, LO)` via 32-step shift-and-add into a
  64-bit accumulator. Booth's algorithm if we want to be faithful
  to one of H&P's designs; simple shift-and-add is fine for the
  first pass.
- `alu_div(a, b)` → `(quotient, remainder)` via restoring
  division.
- Unsigned variants (`multu`, `divu`) — same algorithm, no sign
  manipulation.

These are the most expensive at runtime — ~64 iterations each.
Still fine for stepping.

### Tier 4 — floating point

- Significantly bigger. Three sub-tasks per FP op (add/sub/mul/div):
  unpack into sign/exponent/mantissa, perform the op (which
  internally needs the integer ALU for mantissa arithmetic),
  re-pack with rounding and special-value handling
  (denormals, infinities, NaNs).
- Conversions (cvt.s.w, cvt.w.s, etc.) — integer/FP boundary.
- Comparisons (c.eq.s, c.lt.s, …) — set FCC bits.

This tier is bigger than the previous three combined. Worth a
separate decision after Tiers 1–3 land.

## Where to hook

A new module:

- `src/alu.c` — the gate-level implementations.
- `include/alu.h` — `uint32 alu_add(uint32 a, uint32 b, bool cin,
                                    bool *cout, bool *overflow);` etc.

The integer ALU functions live there. `src/run.c` opcode dispatch
handlers (`Y_ADD_OP` etc.) call `alu_add` instead of doing `a + b`
directly. Same for the other tiers.

## Optional: expose bit-level state for explain mode

The real pedagogical payoff is in narration. If `alu_add` exposes
its internal carry chain (`bool carries[33]`), the explain mode
could emit:

```
  Bit-level addition (ripple-carry):
    bit | a | b | cin | sum | cout
     0  | 1 | 0 |  0  |  1  |  0
     1  | 0 | 1 |  0  |  1  |  0
     2  | 0 | 1 |  0  |  1  |  0
    ...
```

Or even just summarize: "32-bit ripple-carry adder: 5 carries
propagated; overflow = false."

This would slot in as a new L5 or as a sub-mode of L4 in the
existing `-explain` ladder.

## Risks / open questions

- **Speed.** Software-modeled ALU is at least 50–100× slower than
  native arithmetic. For a teaching tool where students step
  one instruction at a time, this is invisible. For a `run` of
  a large program (e.g. a sort), it'd be noticeable. Worth a
  CLI flag to opt out: `-fast-alu` skips the simulation and
  falls back to host arithmetic.
- **Testing parity.** Every ALU function needs to produce bit-exact
  results against the host arithmetic version. The existing test
  suite (`tt.bare.s`, `tt.core.s`, `tt.le.s`) gives some coverage;
  a per-ALU-function unit test (random inputs, compare against
  C `+` / `*` / etc.) would catch divergences.
- **Floating point edge cases.** IEEE 754 rounding modes, denormals,
  NaN propagation — these are where a hand-rolled FP unit goes
  wrong. The simplest correct implementation uses host `float` /
  `double` for the inner math but unpacks/repacks the IEEE 754 bit
  layout explicitly so the student sees how it's encoded. A
  fully-from-scratch FP unit (mantissa arithmetic via the integer
  ALU) is a much bigger project — defer.
- **MIPS vs H&P quirks.** H&P's pedagogical ALU sometimes diverges
  from real MIPS32 in subtle ways (Booth multiplier vs. real
  hardware variants). Pick the H&P design as the reference and
  document the divergence.

## Effort estimate

Without FP:
- Tier 1: ~1 week of focused work.
- Tier 2: ~2–3 days.
- Tier 3: ~1 week (mult/div + careful sign handling).

With FP (Tier 4): an additional 2–3 weeks.

Total without FP: ~3 weeks. With FP: ~5–6 weeks. These are
"continuous focused work" estimates; real calendar time is
typically 2–3× given test/debug cycles.

## First concrete step

If this gets picked up: start with `alu_add` alone. Write the
ripple-carry adder + overflow detection, hook it into Y_ADD_OP /
Y_ADDU_OP / Y_ADDI_OP / Y_ADDIU_OP, build, run all three regression
tests. If the tests pass, the integration pattern is validated and
the rest of Tier 1 is mechanical. If they don't, debug *before*
expanding the surface.
