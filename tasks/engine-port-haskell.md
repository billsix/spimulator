# Port the simulator engine to GHC Haskell, equivalent to the C engine

**Status:** proposed — needs go-ahead
**Priority:** 7
**Difficulty:** 8
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Part of:** `tasks/engine-ports.md`
**Depends on:** `tasks/engine-port-prereq-c-fixes.md`; the shared harness from `tasks/engine-port-python.md` (Plan step 2)

## BLUF

Write a GHC Haskell implementation of the spimulator engine that behaves equivalently to the C
engine, the standard: byte-identical stdout, stderr and exit status on the whole shared corpus, as
checked by the differential harness (`tasks/engine-ports.md`). The maintainer wants to see how a
pure functional language handles a stateful machine. The language study's one blocker, GHC printing
`%.18g` wrongly, is solved and measured (Findings). Done means: the harness reports zero differences
for the Haskell engine, and it builds offline in the image with GHC's own libraries only.

## Context

- `tasks/engine-ports.md` — the shared rules: the C engine is the standard, what equivalent means,
  the layout (`port/haskell/`), the milestone order.
- `tasks/reference/engine-port-language-choice.md` — why the study did not recommend Haskell
  (formatting, state threading), and the measured `%.18g` fix.
- `tasks/review/2026/11/10/repo-review.md` §5 — the behavior inventory to match.
- `tasks/adhoc/engine-port-haskell/` — the formatter proof (run `bash tasks/adhoc/engine-port-haskell/run.sh`).

## Findings (2026-11-10)

**The printing issue, and the fix.** GHC's `Text.Printf` `%.18g` prints `0.300000000000000040` for
`0.1 + 0.2`, where glibc prints `0.300000000000000044` (re-measured with GHC 9.10.3). GHC formats
from the *shortest* digits that read back as the same Double (`floatToDigits`:
`0.30000000000000004`) and pads them with zeros. glibc formats the Double's *exact* binary value,
rounded to the requested digits. `Numeric.showFFloat (Just n)` starts from the same shortest digits,
so it has the same problem.

The fix is a small pure-Haskell formatter. `toRational` turns a Double into its exact value, a
`Rational`. From there, `%.Pg` and `%.Pf` are integer arithmetic:
1. Find the decimal exponent `X` by exact comparison with powers of ten.
2. Round `|x| / 10^(X-P+1)` to an integer, ties to even (glibc's default rounding mode, applied to
   the exact value).
3. Carry into the next power of ten when the rounding overflows.
4. Choose fixed or exponential notation by C's rule (`X < -4 || X >= P`), strip trailing zeros, and
   print a two-digit-minimum exponent.

`inf`/`-inf`, and `nan`/`-nan` by the sign bit, are special-cased. The `#` flag keeps trailing zeros
and the point, and a width pads on the right.

The C engine prints floats in five formats:
- `%.8f` and `%.18g`: the `print_float`/`print_double` syscalls (`src/syscall.c`);
- `%.17g` and `%g`: the AST dump (`src/ast.c`);
- `%.18g` and `%g`: the data listing and the REPL `print` (`src/spim.c`);
- `%#-13.6g`: the FP register display (`src/display-utils.c`).

`tasks/adhoc/engine-port-haskell/CFormat.hs` (about 90 lines) implements all five. On 158,029
values it is **byte-identical to glibc `printf`** in every one of them. The values were random
64-bit patterns, values near every decimal exponent from 1e-30 to 1e30, subnormals, the extremes,
±0, ±inf, ±nan, eighths and single-precision values.

**Other risks, with the planned answer:**
- **Fixed-width integers:** `Data.Int`/`Data.Word` (`Int32`, `Word32`, …) wrap like C's unsigned
  types. Use `quot`/`rem`, which truncate like C, never `div`/`mod`, which floor.
  `Data.Bits.shiftR` on `Int32` is arithmetic and on `Word32` logical, which matches MIPS `sra`/`srl`
  when the type is chosen per instruction.
- **Single precision:** GHC's `Float` is real IEEE single, so `add.s` rounds like C without
  emulation. This is an advantage over Python. Bit casts are `castWord32ToFloat` /
  `castFloatToWord32` (`GHC.Float`), and widening is `float2Double`.
- **What C leaves undefined:** FP→int conversion of NaN or an out-of-range value is undefined in C
  (x86 gives `0x80000000`; review §3.22), and so is GHC's `truncate`. Match the C engine's observed
  value explicitly, as the Python port must.
- **Mutable machine state:** registers and memory segments live in `IOUArray`s (`Data.Array.IO`),
  the scalar state in a record of `IORef`s, and the executor runs in `IO`. This is the clearest
  transcription of a stateful machine. The pure parts are pure functions: decoding, the assembler's
  parse to an AST, the formatter, the arithmetic. That pure/stateful split is the lesson this port
  can teach that the Python one cannot.
- **Parser:** `parsec` (it ships with GHC), mirroring `parser.c`'s grammar and error messages.
  Message text must match C byte for byte, so error paths come straight from the C code, not
  parsec's default messages.
- **REPL:** `haskeline` (it ships with GHC) for line editing, the counterpart of `-Dline_editing`.
  The harness drives the REPL without a TTY, so the non-interactive path must match C exactly.
- **Speed:** not a goal. The harness corpus must still finish inside the meson test timeout.

GHC 9.10.3 (Fedora `ghc-compiler`) ships `base`, `array`, `containers`, `bytestring`, `text`,
`mtl`, `transformers`, `parsec`, `haskeline`, `process`, `directory` and `filepath`. The port uses
only these, so it builds offline with `ghc --make` or `cabal build --offline` and needs no Hackage
download.

## Plan

1. Image: add `ghc` through a new `entrypoint/0N-install-haskell.sh` group script, dispatched by the
   Dockerfile under a `BUILD_HASKELL_PORT` flag (the Makefile defaults it to `1`, the Dockerfile to
   `0`, per the template). It is a gate flag, so `MINIMAL_IMAGE` does not drop it. GHC adds about
   1 GB, which is why it gets its own flag.
2. `port/haskell/`: a cabal package with only GHC's own libraries; `meson.build` builds it and
   registers the engine with the shared harness.
3. Milestones in the umbrella's order, each gated by the harness: arithmetic (with `CFormat` promoted
   from the ad-hoc proof, plus its C-vs-Haskell check as a test) → assembler (AST-parity bytes) →
   executor + syscalls → CLI/REPL → teaching mode.
4. Style: `-Wall -Werror`, every top-level binding with a type signature, `hlint` if it can run
   offline. No Haskell coding-standard doc exists in github.com/billsix/runClaudeInContainer yet;
   write the port's conventions in `port/haskell/README.md`, and propose a shared standard doc only if
   a second Haskell project needs it.

## Decisions

- Port to GHC Haskell as well as Python; both must behave equivalently to the C engine, the standard
  (William Emerison Six <billsix@gmail.com>, 2026-11-10).
- Print floats with the exact-`Rational` formatter (Findings), not `Text.Printf` or a C FFI call to
  `snprintf`. FFI would match trivially, but the port would then depend on the C library for the
  output it is being compared on. The agent's call; revisit only if the formatter shows a difference.
