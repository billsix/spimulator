# Integer-conversion hardening — `-Wsign-conversion`/`-Wconversion` gate for the core

**Status:** COMPLETE (2026-10-16)
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

## What landed (2026-10-16)

The core (`src/`+`include/`) is **clean under clang `-Wconversion -Wsign-conversion`**, and those flags
are now in the core gate (`meson.build`, scoped to the `spimulator` target, clang-only — see the gate
note below). Verified: clang clean under `-Werror`; `meson test` **36/36** (incl. the byte-identical
`ast_parity`/`ast_parity_all` memory goldens); UBSan-trap and ASan gates **25/25**; gcc build still
green. Behavior is byte-identical — every edit is an explicit cast to the type the compiler was already
implicitly converting to (a value-level no-op), or a one-line type/macro fix.

**How, by class (~340 conversions):**
- **Repeated ALU idioms fixed once** — branch-target (`PC + (mem_addr)BRANCH_OFFSET`), effective-address
  (`(u_reg_word)IOFFSET`), applied uniformly.
- **Macro-internal conversions fixed at the macro** (the key leverage — a call-site cast can't reach
  inside a macro): `ROUND_UP`/`ROUND_DOWN` capture `B` as `typeof(V)` (`spim.h`); `BRANCH_OFFSET`'s
  `sign_ex` arg made explicit (`instruction.h`); `CP0_ExCode` and `FCC` cast their `reg_word` operand
  (`registers.h`).
- **Per-site explicit casts** elsewhere (gpr/register reinterpretations → `reg_word`/`u_reg_word`,
  host sizes → `size_t`, addresses → `mem_addr`).
- **Narrowing (`-Wshorten-64-to-32` / `-Wimplicit-int-conversion`)**: `long`/`ssize_t` results cast to
  `reg_word`/`mem_addr`/`int32_t` at the point of truncation; `parser.c` `long dist` → `int dist`
  (eval_imm_expr already returns int32_t, range-checked 0..31).

**The gate is clang-scoped, on purpose.** Every Dockerfile build and both sanitizer gates use
`CC=clang`, and the core is clang-clean. gcc's `-Wsign-conversion` is stricter and flags ~46 additional
edge cases (notably `memory.c` memory-mapped-IO register tests and a few `registers.h` macros), so
enabling the flags under gcc would break a host gcc build. The gate therefore adds the conversion flags
only when `cc.get_id() == 'clang'`; plain `-Werror` still applies under both compilers.

**Known-remaining (not gated, documented):**
- The ~46 gcc-only `-Wsign-conversion` edge cases above — address them if a gcc-enforced conversion
  gate is ever wanted (same cast policy; a few are macro-internal in `registers.h`).
- The `#ifdef SPIM_BIGENDIAN` paths in `memory.c` (e.g. 369/378, 424/435) carry the same latent
  conversions as their little-endian twins but are not compiled on the little-endian gate build, so
  they were left untouched (unverifiable here). Mirror the LE casts if BE is ever gated.
- Pre-existing latent narrowing `scanner.c` `(int)(len + 16)` in two `xmalloc` calls — flagged by a
  reviewer, left as-is (separate correctness review, not a conversion-warning fix).

## Why it was deferred originally (historical)

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
