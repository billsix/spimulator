# Hardened the core's integer conversions and gated `-Wconversion`/`-Wsign-conversion`

**Created:** 2026-10-16 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-10-16

The simulator core (`src/` + `include/`) was made clean under `-Wconversion -Wsign-conversion`,
and those flags were added to the core `-Werror` gate so silent narrowing and sign-flip bugs
became compile errors. The work was split out of the pass-2 modernization
(`tasks/archive/2026/10/16/c23-modernization-pass2.md`, Tier 3) because it was not a safe
mechanical sweep: the warnings concentrated in the MIPS instruction-execution core, where
signed/unsigned casts are the architecture's semantics. The durable model and gate were
harvested into `tasks/reference/simulator-integer-types.md`; read that before touching the
core's integer arithmetic.

## Why it needed per-site review

A probe on 2026-10-16 found 371 warnings, 358 of them `-Wsign-conversion` and 326 in `src/run.c`.
They were conversions between the domain typedefs (`reg_word` = `int32_t`, `u_reg_word`/`mem_addr` =
`uint32_t`) inside arithmetic, the deliberate handling that distinguishes `add`/`addu`, `sra`/`srl`,
`slt`/`sltu` and branch-offset sign extension. A wrong cast would silently change executed behavior,
and the regression suite does not exercise every instruction. This is the
opt-out-where-the-obvious-fix -is-wrong case described in the c-modernization-sweep.md reference doc
of github.com/billsix/runClaudeInContainer.

## Tooling and catalog

- `clang -Xclang -ast-dump[=json]` showed every subexpression's type and each `ImplicitCastExpr`
  with its `CastKind`, so the from/to types at a site were visible.
- `clang-query -p <builddir> src/run.c` with `m implicitCastExpr(hasCastKind("CK_IntegralCast"))`
  found 897 integral casts in `run.c`, of which 190 were the `-Wsign-conversion` hits.
- The compiler diagnostics were the worklist. An ad-hoc `discover.sh` + `analyze.py` pair (under
  `tasks/adhoc/integer-conversion-hardening/`, since deleted) built the target with the flags and
  wrote a `conversions.tsv` catalog and a summary.

All 336 flagged conversions under clang 22.1.8 collapsed into four type-pair boundaries, so a policy
was decided per boundary rather than per site:

| count | from → to | what it is |
| --- | --- | --- |
| 156 | `int` → `unsigned int` | `reg_word`/`mem_word` → `mem_addr`/`u_reg_word` (the MIPS word boundary) |
| 109 | `unsigned int` → `int` | the reverse |
| 45 | `int` → `unsigned long` | `int` → `size_t` (host sizes/indices) |
| 26 | `short` → `unsigned int` | the `int16_t` imm fields → unsigned |

## What was done (~340 conversions)

Every edit was an explicit cast to the type the compiler was already converting to implicitly (a
value-level no-op) or a one-line type/macro fix, so behavior stayed byte-identical.

- Repeated ALU idioms were fixed once and applied uniformly: branch target
  (`PC + (mem_addr)BRANCH_OFFSET`) and effective address (`(u_reg_word)IOFFSET`).
- Conversions inside macros were fixed at the macro, since a call-site cast cannot reach them:
  `ROUND_UP`/`ROUND_DOWN` capture `B` as `typeof(V)` (`spim.h`), `BRANCH_OFFSET`'s `sign_ex`
  argument was made explicit (`instruction.h`), and `CP0_ExCode` and `FCC` cast their `reg_word`
  operand (`registers.h`).
- Elsewhere, per-site casts: register reinterpretations to `reg_word`/`u_reg_word`, host sizes to
  `size_t`, addresses to `mem_addr`.
- Narrowing (`-Wshorten-64-to-32`, `-Wimplicit-int-conversion`): `long`/`ssize_t` results were cast
  to `reg_word`/`mem_addr`/`int32_t` at the truncation point, and `parser.c`'s `long dist` became
  `int dist` (`eval_imm_expr` already returns `int32_t`, range-checked 0..31).

The gate was first added clang-only, since the image and sanitizer gates build with `CC=clang`.
gcc's `-Wsign-conversion` is stricter and flagged ~46 more sites, mostly compound assignments
(`reg_word |=/&= unsigned-mask`) on CP0 and memory-mapped-IO registers plus the `SET_FCC` macro.
Those were fixed too: `recv_control`/`trans_control` were retyped to `uint32_t`, and the CP0
set/clear, `SET_FCC` and field-extract sites were made explicit. The flags are now added
unconditionally (`cc.has_argument`-guarded) in `meson.build`, scoped to the `spimulator` target.

## Results

gcc and clang both built clean under `-Werror` plus the gate; `meson test` passed 36/36, including
the byte-identical `ast_parity`/`ast_parity_all` memory goldens; the UBSan-trap and ASan gates
passed 25/25.

## Split out, not done here

- `tasks/big-endian-conversion-hardening.md` (blocked on big-endian hardware): the
  `#ifdef SPIM_BIGENDIAN` paths in `memory.c` are never compiled on the little-endian host, so their
  conversions were left untouched.
- `tasks/archive/2026/11/11/scanner-xmalloc-int-narrowing.md`: the inner `(int)(len + 16)` narrowing
  in two `scanner.c` `xmalloc` calls, since removing it is a logic change.

The pre-work survey had also listed `include/symbol-table.h`'s `long addr` field, which holds 32-bit
addresses but doubles as a signed constant store (`const_flag`), so narrowing it to `mem_addr` could
change negative or large constants. It was neither narrowed nor split: the field is still
`long addr`, and the gate went clean with explicit casts at its uses.

## Related

- `tasks/archive/2026/06/16/ubsan-sweep.md`: the integer-UB surface this work coordinated with.
- The c-coding-standard.md reference doc of github.com/billsix/runClaudeInContainer
  (integer-conversion rule).
