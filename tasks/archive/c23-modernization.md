# C23 modernization — what landed

## Status — closed (May 2026)

All twelve planned phases shipped on the `c23modernization` branch.
Each phase is a separate commit; together they touch ~150 sites
across the headers and ~50 sites across `src/`.  Verified clean by
the full 22-test regression suite, plus an ASan / UBSan / valgrind
audit (see "Post-merge audit" near the bottom of this doc).

This document is the final state — describes what's in use, the
design decisions worth knowing, and the small set of followups
that surfaced during execution.  No further C23-specific work is
planned.

Migration of the spimulator codebase to take fuller advantage of C23.
The project's meson configuration was already on `c_std=gnu23` (GCC
16.1.1+); the work documented here is consumption of the language
features that opt-in enabled.

## Build configuration

`meson.build` carries these C-language flags:

```
'-Wno-zero-length-array',     # spim uses flexible array members
'-Wunused-function',          # catches dead static fns
'-Wshadow',                   # inner-scope rebinding
'-Wimplicit-fallthrough=5',   # only [[fallthrough]] counts; comments don't
```

`c_std=gnu23` (in `default_options`).  `gnu23` rather than plain `c23`
preserves the GNU statement-expression extension used by the
`MIN`/`MAX`/`ROUND_UP`/`ROUND_DOWN` macros in `include/spim.h`;
`__extension__` is applied at each site to silence `-Wpedantic`.

## C23 features in use

### `[[noreturn]]` attribute

Applied to functions that don't return:
`fatal_error` (`include/spim.h`), `top_level`, `control_c_seen`,
`run_error` (`src/spim.c`).  Replaces the C11 `_Noreturn` keyword.

### `[[nodiscard]]` attribute

Applied to functions where ignoring the return value is almost
certainly a bug — primarily heap-allocating constructors and
significant status returns:

- 26 `ast_make_*` constructors in `include/ast.h`
- 8 heap-allocators in `include/inst.h`: `const_imm_expr`, `copy_inst`,
  `incr_expr_offset`, `inst_decode`, `inst_to_string`, `make_addr_expr`,
  `make_imm_expr`, `set_breakpoint`
- `undefined_symbol_string` (`include/sym-tbl.h`)
- `str_copy`, `xmalloc`, `zmalloc`, plus the previously-marked
  `read_assembly_file`, `run_program` (`include/spim-utils.h`)
- `ss_to_string` (`include/string-stream.h`)
- `run_spim` (`include/run.h`)

Two legitimate-discard call sites carry an explicit `(void)` cast
to acknowledge the intent:

- `src/spim.c` batch-mode `run_program` — breakpoint-hit signal
  is meaningless without a REPL.
- `src/run.c` `JUMP_INST` macro — delay-slot continuation is owned
  by the outer `run_spim`.

Pure query functions (`opcode_is_*`, `mem_read_*`, etc.) are not
marked.  Discarding wastes work but doesn't leak; left for a possible
later broadening pass.

### `[[fallthrough]]` attribute + strict warning

`-Wimplicit-fallthrough=5` is enabled in `meson.build`.  Level 5 accepts
only the C23 attribute; comment-style annotations no longer count.
The codebase happened to have zero implicit fallthroughs at the time
of conversion (every switch case ended in `break`/`return`/`goto`),
so no annotations were needed.  The warning's value is forward-looking:
any future implicit fallthrough is a compile-time signal.

### `enum E : T` — explicit underlying types

Applied to every named enum, sized to the smallest type that fits:

| Enum | Width | Reason |
|---|---|---|
| `parse_mode_t` | `uint8_t` | 2 values |
| `ast_kind` | `uint8_t` | ~23 values; ast_node packs more tightly |
| `ast_label_kind` | `uint8_t` | 2 values |
| `asm_event_kind` | `uint8_t` | 11 values |
| `asm_segment` | `uint8_t` | 4 values |
| `op_type` | `uint8_t` | values 0-42 |
| `mips_exc_code` | `uint8_t` | values 0-30 |
| anonymous TOK_* (`include/tokens.h`) | `int32_t` | values start at 256; tokens flow through `int` slots everywhere |

The width choice is per-enum, sized to the smallest stdint type that
holds the enumerators.  The TOK_* enum is wider because its values
flow through `int`-typed scanner/parser variables — narrowing would
add conversion noise without struct-packing benefit.

### Two `#define` clusters promoted to enums

The plan's most consequential reshape was turning two families of
`#define`s into real enum types so they participate in `-Wswitch`
coverage:

- **`enum op_type`** (`include/op-types.h`).  Was 25 type-tag
  `#define`s in `include/op.h` (`ASM_DIR`, `PSEUDO_OP`,
  `BC_TYPE_INST`, `R3_TYPE_INST`, ..., `NOARG_TYPE_INST`).  The
  `op_type_entry.type` field in `src/parser.c` is now typed, and
  switches on it benefit from coverage checking.

- **`enum mips_exc_code`** (`include/inst.h`).  Was 20 exception-code
  `#define`s.  `raise_exception(int)` was retyped to
  `raise_exception(mips_exc_code)`; the 21 `RAISE_EXCEPTION(...)`
  call sites pass enumerators that convert implicitly.

#### The op.h split

The enum conversion forced a structural change: `tokens.h` does
`#include "op.h"` *inside* the body of its anonymous `enum : int32_t
{ TOK_EOF = 256, ... }` to splat the TOK_* names via X-macro
expansion.  A `typedef enum` block inside another enum body is a
syntax error.

The split:

- **`include/op-types.h`** holds the typedef enum.  File-scope only.
  Included by `inst.c`, `parser.c`, `scanner.c` — the consumers of
  the type tag names.

- **`include/op.h`** is now pure X-macro `OP()` content with no
  preprocessor guard, no typedefs.  Safe to `#include` inside another
  enum body.

`src/dump_ops.c` (a standalone utility, not in the regular meson
build) defines `OP(a, b, c, d) {a, d}` which discards the type tag,
so it doesn't need `op-types.h`.

### `constexpr` for typed compile-time constants

~85 `#define`s migrated to `constexpr` declarations across four
headers:

- **`include/spim.h`** — `K`, `BYTES_PER_WORD`, sizing constants
  (`TEXT_SIZE`, `DATA_SIZE`, `STACK_SIZE`, the K_ variants, the
  `_LIMIT` variants), `DEFAULT_RUN_STEPS`, intervals
  (`IO_INTERVAL`, `RECV_INTERVAL`, `TRANS_LATENCY`, `TIMER_TICK_MS`),
  `SMALL_DATA_SEG_MAX_SIZE`, `DIRECT_MAPPED`, `TWO_WAY_SET`.

- **`include/mem.h`** — `TEXT_BOT`/`DATA_BOT`/`STACK_TOP`/`K_TEXT_BOT`/
  `K_DATA_BOT` as `constexpr mem_addr`; `MM_IO_BOT`/`MM_IO_TOP` and
  the `RECV_*` / `TRANS_*` register addresses; `TEXT_CHUNK_SIZE`.
  Status-bit flags (`RECV_READY`, `RECV_INT_ENABLE`, `TRANS_READY`,
  `TRANS_INT_ENABLE`) as `constexpr uint32_t`.

- **`include/inst.h`** — `IMM_MIN`/`IMM_MAX` as `constexpr int32_t`
  (corrected to the signed-literal form `-0x8000`/`0x7fff` from the
  prior `0xffff8000`/`0x7fff`).  `UIMM_MIN`/`UIMM_MAX` as `constexpr
  uint32_t`.  FP-compare flag bits `COND_UN`/`COND_EQ`/`COND_LT`/
  `COND_IN` as `constexpr uint32_t`.

- **`include/reg.h`** — register-array lengths (`R_LENGTH`,
  `FGR_LENGTH`, `FPR_LENGTH`), all `REG_*` register-number constants,
  all `CP0_*_Reg` selectors, `FIR_REG`, `FCSR_REG`, `CC0_bit`/
  `CC1_bit` as `constexpr int`.  The `CP0_Status_*` (13 values),
  `CP0_Cause_*` (12 values), `CP0_Config_*` (4 values), `FIR_*` (3
  values), `FCSR_FCC`, `FCSR_Cause_*` (6), `FCSR_Enable_*` (5), and
  `FCSR_Flag_*` (5) bitmask families as `constexpr uint32_t`.  The
  composite masks (`CP0_Status_Mask`, `CP0_Cause_Mask`, `FIR_MASK`,
  `FCSR_MASK`, `CP0_Config_Mask`) are constexpr-derived-from-constexpr
  (their initializers reference the earlier constexpr names directly).

Several `#define`s remain because constexpr can't reach them:
- `EXCEPTION_ADDR` (in `include/spim.h`) — selected by `#ifdef MIPS1`
  at preprocessing time.
- `DEFAULT_RUN_LOCATION`, `END_OF_TRAP_HANDLER_SYMBOL` — string
  literals.
- All the field-extraction macros in `include/inst.h` (`OPCODE`,
  `RS`, `RT`, `RD`, etc.) — these are parameterized accessors, not
  constants.

#### Linkage and bloat note

C23 file-scope `constexpr` objects have internal linkage and static
storage.  Each translation unit including `reg.h` (or another
constexpr-bearing header) emits its own copy of each constant into
`.rodata`.  At `-O2` the optimizer folds them away (verified: zero
duplicate symbols in the release binary).  At `-O0` they appear as
real symbols — useful for debugger inspection (`(gdb) p CP0_Status_CU`
resolves to the value), but it does grow the rodata segment of debug
builds.  Release-build binary growth was +20KB total (about +5.5KB
from `#embed` plus the rest from inline-function expansion described
below).

### `static inline` functions + `typeof` macros

Function-like macros in `include/spim.h` migrated:

- `streq(s1, s2)` — was `#define streq(s1, s2) !strcmp(s1, s2)`,
  now `static inline bool streq(const char* a, const char* b) { return
  strcmp(a, b) == 0; }`.  Reads `== 0` instead of `!strcmp`; type-checked.

- `SIGN_EX(X)` → `sign_ex(int x)` — `static inline int32_t`.  Two call
  sites updated (`include/inst.h:IDISP` macro, `src/sym-tbl.c:240`).

- `MIN(A, B)` and `MAX(A, B)` — `typeof`-based statement-expression
  macros that capture each argument into a local before comparing.
  Eliminates the double-evaluation hazard (`MIN(rand(), x)` now calls
  `rand()` once).  Polymorphic across `int`/`mem_addr`/`size_t`.

- `ROUND_UP(V, B)` and `ROUND_DOWN(V, B)` — same `typeof` macro shape
  (B was previously multi-evaluated as `(B - 1) & ~(B - 1)`).

The `MIN`/`MAX`/`ROUND_*` macros use the GNU statement-expression
construct, allowed by `c_std=gnu23` but not standard C; each is
prefixed with `__extension__` to silence `-Wpedantic` warnings.

### `<stdbit.h>`

`src/run.c` uses `stdc_leading_ones((uint32_t)R[RS(inst)])` and
`stdc_leading_zeros(...)` for the `TOK_CLO_OP` and `TOK_CLZ_OP`
instructions.  Replaces 9-line hand-rolled bit-scan loops; GCC
compiles to single `lzcnt`/`tzcnt`-class instructions on x86-64.

GCC 16's `<stdbit.h>` ships C-type-suffixed variants (`_uc`/`_us`/
`_ui`/`_ul`/`_ull`), not fixed-width-suffixed (`_ui32`/`_ui64`).
The type-generic forms `stdc_leading_zeros(x)` / `stdc_leading_ones(x)`
dispatch by argument type — used here to avoid depending on host
`sizeof(unsigned int) == 4`.

### `<stdckdint.h>`

`src/run.c` uses `ckd_add(&sum, ...)` and `ckd_sub(&diff, ...)` for
MIPS `add`/`addi`/`sub` overflow detection.  Three call sites total.
Replaces a hand-rolled `SIGN_BIT`-based check that performed the
wrapping add first, then inspected sign bits — a pattern that
technically invokes signed-integer-overflow UB under `-O2`.

MIPS `addu`/`subu` (which the spec defines as non-trapping) remain
as bare `+`/`-` operators on `u_reg_word` casts.  The `SIGN_BIT`
macro is kept; it's still used by the sign-aware branch instructions
(`bgez`/`bgtz`/`blez`/`bltz` and their AL/L variants).

### `#embed`

The default exception handler is baked into the binary at compile
time:

```c
static const unsigned char embedded_exceptions_bytes[] = {
#embed "exceptions.s"
};
```

(in `src/spim-utils.c`.)

Dispatch design (in `initialize_world`):
- `exception_file_name` defaults to a sentinel pointer
  (`SPIM_DEFAULT_EXCEPTIONS_SENTINEL`, declared `extern char[]` in
  `include/spim-utils.h`).
- Pointer-identity match against the sentinel → load via `fmemopen`
  over the embedded bytes.
- Any other pointer value (env var, CLI flag) → existing `fopen`
  path.
- `-noexception` is a separate flag that skips both paths.

The binary is now self-contained.  Running from a build tree, an
arbitrary directory, or anywhere else works without a reachable
`exceptions.s` on disk.

`install_data('src/exceptions.s', ...)` in `meson.build` is kept
unchanged.  The file is no longer load-bearing at runtime but
serves as documentation in the install tree and as a starting point
for users who want to customize via `-exception_file`.

### Empty initializers

`= {0}` replaced with `= {}` where the intent is default-initialize:
`src/scanner.c:23` (`scan_value_t scan_value = {};`) and `src/spim.c:40`
(`static char history_path[1024] = {};`).  Two sites — the rest of
the zero-init in the codebase happens via `memset` in dynamic
allocation paths.

### `static_assert`, `bool`/`true`/`false`, `nullptr`

These C23 features were already in pervasive use before this work
landed (covered in the earlier Phase 3 portability cleanup and the
AST migration).  `static_assert` appears in `include/mem.h:19`
asserting `sizeof(mem_word) == BYTES_PER_WORD`; `nullptr` covers
~250 sites; `bool`/`true`/`false` are used as keywords throughout
with no `<stdbool.h>` includes.

## Design decisions worth knowing

### `RAISE_EXCEPTION` keeps its bare-brace expansion

`include/inst.h:RAISE_EXCEPTION(EXCODE, MISC)` is the only
multi-statement macro in the tree that's not wrapped in
`do { ... } while (0)`.  The `MISC` parameter is a statement
injected at the call site's enclosing scope — most commonly `break;`
(to terminate a switch case in `run_spim`) or `return true;` (early
return).  Wrapping in `do { } while (0)` would re-scope `break` to
the do-while loop instead of the enclosing switch, silently leaving
the case body running.

A comment block at the macro definition documents this constraint.
Future cleanup option: eliminate the macro entirely and have call
sites write `raise_exception(X); break;` directly — would land 21
mechanical replacements.

### `find_op_type` returns `int`, not `op_type`

`src/parser.c:find_op_type` returns `-1` to signal "not found,"
which isn't representable as a `uint8_t`-backed `op_type`.  Switching
to a `OP_TYPE_INVALID = 0xff` sentinel in the enum and returning
`op_type` would be defensible but adds an enumerator that has no
meaning in any other context.  Returning `int` is the smaller move.

### The op.h X-macro design constraint

`include/op.h` contains no preprocessor guards, no typedefs, no
`#define`s — only the raw `OP(...)` X-macro entries.  Anything
stateful breaks either re-inclusion (typedef redefinition) or use
inside another enum body (which `tokens.h` does).  The file's
top-of-file comment records this constraint.

## What didn't land

Items the modernization plan considered but deferred or skipped:

- **`auto` for type inference** — readability harm in a teaching
  simulator outweighs keystroke savings.  No conversion.
- **`_BitInt(N)` for exact-width integers** — `int32_t`/`uint32_t`
  already cover spim's needs.
- **Digit separators (`1'000'000`)** — cosmetic.  Available for ad
  hoc use; no sweep.
- **`u8` / `char8_t`** — no Unicode work in the simulator core.
- **`[[reproducible]]` / `[[unsequenced]]`** — experimental
  attributes; not used.
- **Switching `c_std` from `gnu23` to plain `c23`** — the GNU
  statement-expression extension is load-bearing for the `MIN`/`MAX`/
  `ROUND_*` macros.
- **Broader `[[nodiscard]]` to pure-query functions**
  (`opcode_is_*`, `mem_read_*`, `current_text_pc`, etc.) — discarding
  these wastes work but doesn't leak.  Possible later pass.
- **`unreachable()` for impossible default cases** — wanted to wait
  until the enum-tag sweep has bedded in.  Several `default:` cases
  in `src/run.c` and `src/parser.c` are reachable for adversarial
  input (bad opcodes from a binary file); only the type-system-
  guaranteed-impossible ones are candidates.  None applied.

## Followup items worth filing

Open follow-on tasks documented as separate plan files in `tasks/`:

- [`../sym-tbl-hash-overflow.md`](../sym-tbl-hash-overflow.md) —
  signed-int overflow in the symbol-table hash function
  (`sym-tbl.c:76`).  Pre-existing UB (Larus, 2023), not from the
  C23 work, but surfaced cleanly by ASan during the post-sweep
  audit.  Trivial fix.

- [`../scanner-hex-shift-overflow.md`](../scanner-hex-shift-overflow.md)
  — left-shift overflow when parsing 32-bit hex literals with the
  high nibble bit set (`scanner.c:239`).  Pre-existing UB from the
  parser migration, surfaced by ASan.  Trivial fix.

- [`../parser-leak-cleanup.md`](../parser-leak-cleanup.md) —
  224-byte parser/scanner allocation leak at exit (scales with
  program size, bounded per parse).  Pre-existing.  Three options
  outlined; recommended approach leans on the existing AST
  ownership model.

Smaller items not promoted to their own plans:

- **Regression test for `ckd_add` overflow trap.**  The change to
  `ckd_add`/`ckd_sub` is the most semantically load-bearing edit in
  the sweep, but the existing test suite doesn't exercise the
  signed-overflow trap path (only address-fault traps).  Manually
  verified during the sweep with one-off MIPS programs; worth
  encoding as `tests/tt.arith_overflow.s` or similar.

- **Remove `RAISE_EXCEPTION` macro entirely.**  As noted above, the
  macro adds no abstraction over `raise_exception(X); break;` at the
  call site, and its design constraint (bare-brace expansion) is the
  only counterexample to the otherwise-uniform `do { } while (0)`
  convention.  ~21 call sites, mechanical sed.

- **Broader `[[nodiscard]]` sweep** to pure-query functions.  Wave
  of one-time noise expected; do in one commit, not partial.

## Post-merge audit (May 2026)

Verification performed before declaring the branch ready to merge:

- **Regression suite**: 22/22 green at every phase boundary and
  after the final commit.
- **Clean rebuild**: zero warnings under
  `-Wall -Wextra -Wpedantic -Wshadow -Wunused-function
  -Wimplicit-fallthrough=5`.
- **Higher-warning probe** (`-Wnull-dereference -Wdouble-promotion
  -Wstrict-prototypes -Wmissing-prototypes`): two warnings, both
  pre-existing (`parser.c:46` missing prototype on
  `set_input_file_name`; `reg.h:131` macro double-promotion in
  `FPR_S`).  Not from the C23 work; left for a future polish pass.
- **Valgrind** (`./builddir/spimulator -f tests/tt.argv.s ...`):
  `ERROR SUMMARY: 0 errors`.  224-byte / 26-block leak at exit
  matches master exactly (pre-existing).
- **ASan + UBSan** under `meson setup -Db_sanitize=address,undefined`:
  two UBSan findings (both pre-existing, filed as the two `tasks/`
  plans above).  No new sites introduced by the C23 work.
  Memory-error count: 0.
- **Binary size**: release-build delta vs master = +20 KB
  (~8% growth).  +5.5 KB embedded `exceptions.s`; rest is inline
  expansion of `MIN`/`MAX`/`streq`/`sign_ex` at call sites.
- **Debug-build symbol bloat**: file-scope `constexpr` objects emit
  per-TU copies in `.rodata` at `-O0` (~1440 read-only symbols).
  Folded to zero duplicates at `-O2`.  Documented; not a regression.
- **`#embed` self-containment**: confirmed the built binary runs
  from `/tmp/empty` with no `exceptions.s` reachable on disk.
- **Explain-mode line-count drift signal**: 2961 lines on
  `tests/tt.explain.s`, unchanged across the sweep (any drift
  here would indicate the narration shape changed unintentionally).
