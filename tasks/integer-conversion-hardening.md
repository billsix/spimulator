# Integer-conversion hardening — `-Wsign-conversion`/`-Wconversion` gate for the core

**Status:** proposed — needs go-ahead
**Priority:** 6
**Difficulty:** 8
**Created:** 2026-10-16 (William Emerison Six <billsix@gmail.com>)

## BLUF

Make the simulator core (`src/`+`include/`) clean under `-Wconversion -Wsign-conversion` and then add
those flags to the `-Werror` gate, so silent narrowing / sign-flip integer bugs become compile errors.
Split out of the pass-2 modernization (`tasks/archive/2026/10/16/c23-modernization-pass2.md`, Tier 3) because it is **not
a safe mechanical sweep**: the warnings concentrate in the MIPS instruction-execution core where the
signed/unsigned casts *are* the architecture's semantics, so each must be reviewed per instruction, not
blanket-fixed. "Done" = core compiles clean under both flags, the flags are in the gate, and
`ast_parity`/`ast_parity_all` + the sanitizer suites are still green.

## Why this is deferred, not done

A `-Wconversion -Wsign-conversion` probe of the core (2026-10-16) found **371 warnings**, 358 of them
`-Wsign-conversion`, and **326 in `src/run.c` alone**. Inspection showed these are conversions
*between* the domain typedefs (`reg_word` = `int32_t`, `u_reg_word`/`mem_addr` = `uint32_t`) inside
arithmetic — i.e. the deliberate signed/unsigned handling that distinguishes `add`/`addu`, `sra`/`srl`,
`slt`/`sltu`, branch-offset sign-extension, etc. "Fixing" one with a wrong cast silently changes
executed behavior, and the regression suite does not exercise every instruction, so a blind sweep is
unsafe. This is the one place the usual "tighten the integer types" rule must be applied
instruction-by-instruction (`github.com/billsix/runClaudeInContainer:tasks/reference/c-modernization-sweep.md`: opt out where the obvious
fix is wrong).

## Known sub-items

- **`src/run.c` (~326 sites).** Per-instruction: confirm each signed↔unsigned cast matches the MIPS
  spec, add an intentional cast (documented) where correct, fix where it is a genuine narrowing bug.
  Coordinate with the integer-UB surface mapped by the archived UBSan sweep
  (`tasks/archive/2026/06/16/ubsan-sweep.md`) — do the sign/overflow tightening once.
- **`src/symbol-table.h` `long addr`.** The `label.addr` field is a 64-bit `long` holding 32-bit MIPS
  addresses, but it **also doubles as a signed constant store** (`const_flag`; `.set`-style constants
  written via `mem_write_word(pc, sym->addr)` at `symbol-table.c:227`, substituted in `scanner.c:425`/
  `:475`). Narrowing to `mem_addr` (`uint32_t`) changes value/sign for negative/large constants —
  reshape only after proving every constant that reaches `addr` fits, or split address vs constant into
  two fields.
- **`-Wconversion` value-changing sites (~13).** Mostly `long`→`int32_t`/`int` at `strtol`/`ssize_t`/
  `addr` boundaries (`symbol-table.c:227,350`, `syscall.c:127,136`, `instruction.c:252,303,1030,1083`,
  `parser.c:1268-1272`, `spim.c:1393`). Each is a real truncation point; decide the correct width.

## Plan

1. Re-run `tasks/adhoc/integer-conversion-hardening/discover.sh` (regenerates the catalog under
   `data/`); the prep below already did the first categorization. Adjudicate each site correct-vs-bug.
2. Work `run.c` in small, instruction-grouped commits, each verified by `ast_parity`/`ast_parity_all`
   (byte-identical) + the targeted regression tests + the ASan/UBSan suites.
3. Resolve `long addr` (prove-fits or split the field).
4. Add `-Wconversion -Wsign-conversion` to the core's gated flags once clean.

## Prep done (2026-10-16) — tooling + catalog (read-only; no code changed)

The scary part of this task is that C's integer conversions are invisible in the source. Two tools make
them visible, and a third turns them into a worklist:

- **`clang -Xclang -ast-dump` (or `-ast-dump=json`)** prints every subexpression's type AND every
  `ImplicitCastExpr` with its `CastKind` (`<IntegralCast>`, `<LValueToRValue>`, …) — so you can see the
  FROM type, the TO type, and every intermediate at a site, which is what you need to decide whether a
  cast is the intended unsigned-MIPS-word semantics or a bug. Worked example + how to target a site:
  `tasks/adhoc/integer-conversion-hardening/data/worked-example.txt`.
- **`clang-query -p <builddir> src/run.c`** runs AST matchers interactively, e.g.
  `m implicitCastExpr(hasCastKind("CK_IntegralCast"))` (897 integral casts in run.c; the sign-changing
  subset is the 190 `-Wsign-conversion` hits).
- **The compiler diagnostics are the precise worklist.** `discover.sh` builds the spimulator target
  under `-Wconversion -Wsign-conversion` and `analyze.py` parses them into
  `data/conversions.tsv` (file, line, col, flag, from→to) + `data/summary.txt`.

**Key finding — this is 4 boundaries, not 336 puzzles.** All 336 flagged conversions (clang 22.1.8)
are `-Wsign-conversion`, and they collapse into **four** resolved type-pairs:

| count | from → to | what it is |
| --- | --- | --- |
| 156 | `int` → `unsigned int` | `reg_word`/`mem_word` → `mem_addr`/`u_reg_word` (the MIPS word boundary) |
| 109 | `unsigned int` → `int` | the reverse |
| 45 | `int` → `unsigned long` | `int` → `size_t` (host sizes/indices) |
| 26 | `short` → `unsigned int` | the `int16_t` imm fields → unsigned |

So the work is: **decide a policy per boundary** (keep-as-intended-unsigned-semantics with an explicit
cast, or fix a real narrowing), not invent 336 separate answers. 190 of the 336 are in `run.c` (the
ALU); `memory.c` (52) is next. The `int → unsigned long` (size_t) group is the lowest-risk and overlaps
the host-index work; the `run.c` word boundary is the delicate one (needs per-op review vs the MIPS
spec + the ASan/UBSan gates after each chunk).

## Cross-links

- `tasks/archive/2026/10/16/c23-modernization-pass2.md` (parent; Tier 3 did the safe structural widths in `instruction.h`).
- `tasks/archive/2026/06/16/ubsan-sweep.md` (the integer-UB surface to coordinate with).
- `github.com/billsix/runClaudeInContainer:tasks/reference/c-coding-standard.md` (integer-conversion rule), `github.com/billsix/runClaudeInContainer:tasks/reference/c-modernization-sweep.md`
  (why this is opt-out-in-place, not a blanket sweep).
