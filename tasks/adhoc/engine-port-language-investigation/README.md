# Throwaway spikes for tasks/engine-port-language-investigation.md

Each spike implements the same two things and must print byte-identical output:

1. **Edge cases** — the 32-bit integer and IEEE-float semantics a MIPS engine needs, with the
   rules spimulator's C engine uses (`src/run.c`): wraparound, overflow trap detection, arithmetic vs
   logical shift, shift-amount masking, `mult`/`multu` HI/LO, `div` truncation toward zero, the
   divide-by-zero and INT_MIN/-1 "leave HI/LO" rule, `lb`/`lbu` extension, float bit casts
   (`mtc1`/`mfc1`), single-precision rounding, `cvt.w.s` truncation, spim's `round.w.s`, and the
   `print_float` (`%.8f`) / `print_double` (`%.18g`) formats.
2. **A decode + execute loop** over seven real MIPS encodings (lui, ori, addiu, addu, bne,
   syscall) summing 1..3,000,000 mod 2^32 (about 9M instructions), timed.

`ref.c` is the oracle. `run.sh` builds every spike, diffs each output against the C output, and
prints the wall time. Not part of any build; delete with the task archive.
