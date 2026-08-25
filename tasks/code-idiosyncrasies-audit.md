# Audit the code for inherited idiosyncrasies

**Status:** AUDIT COMPLETE 2026-08-25 — findings table below (verified, `file:line`-anchored).
**Awaiting go-ahead to fix** (Method step 3). **One HIGH-severity latent bug found:** `fatal_error()`
(`src/spim.c:1502-1508`) corrupts its own format string via a stray `va_arg`, so every error path
crashes instead of reporting — recommend fixing that first, independent of the cosmetic rest.
**Priority:** 5
**Difficulty:** 4
**Created:** 2026-07-07

## Request (Bill, 2026-07-07)

Double-check a lot of the code for weird idiosyncrasies — the observed example
being a "random `void` argc"-style oddity (nonsensical parameter
types/usages).

**Scope (clarified by Bill, 2026-07-07): the example code too, not necessarily
spimulator source itself.** So the sweep covers, in priority order:
1. `examples/src` — the demo C sources (freestanding, hand-written for
   readability — exactly where a nonsense parameter survives review because
   nothing warns on it) and the hand-written `.asm`.
2. `pgu/src` — the book's C ports and asm.
3. `src/`+`include/` (the simulator) — lower priority; it's been through
   several modernization passes, but the same greps are cheap to run over it.

## What to sweep for

Concrete patterns, each greppable or clang-tidy-able:

- **Nonsense signatures / parameters**: `void`-typed or unused parameters that
  exist only for a dead historical reason (`(void)x;` casts hiding a parameter
  that should be removed; `argc`/`argv` threaded into functions that ignore
  them; find the specific "void argc" Bill saw).
- **K&R-era residue**: implicit-int habits, old-style casts where C23 idioms
  exist, `register`/`extern` noise, `char*` used for byte buffers that should
  be `uint8_t*`.
- **Signature/typedef mismatches**: e.g. functions taking `int` where every
  caller passes a `mem_addr`/`reg_word`; boolean-ish `int`s not yet `bool`.
- **Dead parameters and always-constant arguments**: parameters that every
  caller passes the same literal for.
- **Inconsistent conventions** already flagged in `codebase-cleanup-plan.md`
  Tier B (`read_mem_*`/`set_mem_*` asymmetry, `str_copy` vs `strdup`,
  `*_inst` suffix) — fold those in rather than duplicating them here.
- **Comment/code drift**: comments describing behavior the code no longer has
  (the "op.h" self-references in `opcodes.h`/`opcode-types.h` are one known
  case — tracked in `opcode-types-descriptive-names.md`).

## Method

1. One pass with tooling over the C: `clang-tidy` (readability-*,
   misc-unused-parameters, bugprone-*) over `examples/src`, `pgu/src/c`, and
   `src/`+`include/`, and triage — the image already ships clang-tidy via
   `lint.sh`. Note the demos compile `-nostdlib -ffreestanding`; pass those
   flags so tidy sees them the way the build does.
2. One pass by eye — the `.asm` files have no tooling, so the demo asm gets
   read directly (weird register choices, dead stores, copy-paste residue
   from a neighboring demo). Log findings as a table in this doc (site →
   what's weird → proposed fix → risk).
3. Get a go-ahead on the findings table, then fix in small mechanical commits
   with the full `meson test` suite (and `make image`'s sanitizer gate) as the
   verification gate.

## Relation to other tasks

Overlaps deliberately bounded: naming consistency lives in
`codebase-cleanup-plan.md` Tier B; header hygiene in Tier C; opcode-table
naming in `opcode-types-descriptive-names.md`. This task is the
catch-the-rest sweep for *semantic* oddities, not naming style.

## Findings (audit complete 2026-08-25, all `file:line` verified by direct inspection)

**Summary.** The demo/book layers (`examples/src`, `pgu/src`) are in good shape: argc/argv are handled
legitimately, byte buffers already use `unsigned char`/`int8_t`, and classic K&R residue (implicit-int
defs, `register`, `char*` byte buffers) is *absent* from the modernized parts. The real findings are a
scatter of comment/code drift and copy-paste residue in the hand-written asm, plus a few dead/misnamed
items. The simulator core (`src/`, `include/`) is partially-modernized C23 grafted onto legacy SPIM
bones — and hiding in it is **one genuine latent bug** (`fatal_error`). The **"random void argc"** shape
Bill remembered is real: prominently at `src/dump-opcodes.c:61` (implicit-int `main`, unused
`argc`/`argv`) and, defensibly, at `pgu/src/c/toupper-nomm-simplified.c:89` (`(void)argc;`).

### ⚠ Prioritized call-outs
- **`src/spim.c:1502-1508` — real bug (HIGH).** `fatal_error(char* fmt, ...)` does
  `va_start(args,fmt); fmt = va_arg(args, char*); vfprintf(stderr, fmt, args);` — the `va_arg` line
  **overwrites the format string with the first variadic arg**. All 14 call sites break: bare-string
  calls read a nonexistent vararg as `fmt` (UB); `"…: %d\n", OPCODE(...)` reinterprets an `int` as a
  `char*` fmt (segfault); `"…: %s\n", filename` prints the filename *as* the format. The correct sibling
  `run_error` (spim.c:1512) lacks this line. **Fix:** delete the `fmt = va_arg(...)` line; add
  `va_end(args)`. (Verify via `meson test` + `make image` sanitizer gate.)
- **`src/dump-opcodes.c:61` — the "void argc" case.** `main(int argc, char** argv)` has **no return
  type** (implicit `int`, removed in C23 which the tree targets) and never references `argc`/`argv`.
  Fix: `int main(void)`.

### Scope 1 — `examples/src` (demos: C + hand-written asm)
| file:line | what's weird | proposed fix | risk |
|---|---|---|---|
| `examples/src/io.h:10` | Comment claims `print_int → integer2string + count + os_write`, but `print-int.c` does its own inline unsigned conversion, never calls `integer2string`. | Reword comment to match. | low |
| `examples/src/integer-to-string.c:17` | `integer2string(int value, char*)` takes signed `int` but is unsigned-only (negative → garbage, no sign handling). Also **dead** (kept as a 1:1 asm parallel). | Type param `unsigned int`, or note it's the unsigned book version. | low |
| `.../print1through10/print1through10.asm:22` | Header cites `print1through10-1.c`; no `-1` file exists (source is `print1through10.c`). | Fix reference. | low |
| `.../print1through10/` (name vs behavior) | Named "1 through 10" but emits **0..10** (`li $t0,0`/`bgt $t0,10`); self-acknowledged. | Rename or note the off-by-one intent consistently. | low |
| `.../get-char-from-user-1.asm:80` | Symbol table cites "Bug #3" but the `#NOTES:` block enumerates only Bug #1/#2 (intentionally-buggy teaching file). | Add Bug #3 to NOTES. | low |

_Non-findings (verified OK): `bubble-sort.c:66 (void)argv;` is legit (uses `argc`); os.h multi-arch wrappers and teaching-libc sigs are clean._

### Scope 2 — `pgu/src` (book asm ports + C)
| file:line | what's weird | proposed fix | risk |
|---|---|---|---|
| `pgu/src/c/toupper-nomm-simplified.c:88-89` | `my_main(int argc,char**argv)` then `(void)argc;`; `argv[1]`/`argv[2]` dereffed with no bounds check (crt0 reference — sig is fixed). | Keep sig; consider `if (argc<3) os_exit(1);` or a comment that the shim guarantees argc. | low |
| `pgu/src/power.asm:69` (+45-46,57) | **Dead store** `move $s0,$ra` — written, never read; `main` exits via `li $v0,17;syscall`, but header/symtab describe a `$ra`-restore path that doesn't exist (residue from conversion-program.asm). | Delete line 69; drop the `$s0`/`$ra` claims at 45-46,57. | low |
| `pgu/src/exit.asm:79` | Comment cites the pattern "in helloworld, 02, 03, etc." — no such files in pgu/src. | Cite real files or drop the list. | low |
| `pgu/src/conversion-program.asm:46` | Symtab `buf $a1 (write cursor walks it)` wrong: `$a1` only read as fixed base (92,106); the walking cursor is `$t1`. | Reword: `$a1`=base, `$t1`=cursor. | low |

### Scope 3 — `src/` + `include/` (simulator core)
| file:line | what's weird | proposed fix | risk |
|---|---|---|---|
| `src/spim.c:1502-1508` | **fatal_error format-string corruption** (see call-out). | delete `va_arg` line, add `va_end`. | **HIGH** |
| `src/spim-utils.c:445,454` | `fatal_error("…%d bytes.\n")` — `%d` with no matching arg (independent of the bug). | pass the size. | med |
| `src/dump-opcodes.c:61` | implicit-`int` `main`, unused `argc`/`argv` (the "void argc" shape). | `int main(void)`. | low |
| `src/parser.c:328-332` | `emit_dir_seg(..., bool kernel, ...)` — `kernel` dead (`(void)kernel;`, "implied by kind"); both callers derive from same info. | drop the param + its 2 args. | low |
| `src/spim-utils.c:442,451`; `include/spim-utils.h:51-52` | `xmalloc(int size)`/`zmalloc(int size)` — signed `int` into `malloc`/`calloc` (`size_t`); >2GB/negative misbehaves. | `size_t size`. | med |
| `include/memory.h:109-111` | `expand_data/k_data/stack(int)` signed byte counts (same class). | `size_t`/unsigned. | low |
| `include/spim-utils.h:37`; `src/display-utils.c:26` | `format_registers(..., int print_gpr_hex, int print_fpr_hex)` — the two `int`s are booleans; caller passes a `bool`. | type both `bool`. | low |
| `include/symbol-table.h:43`; `src/symbol-table.c:132` | `record_label(..., int resolve_uses)` — boolean-ish; every caller passes `0`/`1`. | type `bool`. | low |
| `src/spim.c:265-270` | six scanner protos declared locally as `extern`; `scanner_init` also in `include/scanner.h:15` — duplicated. | move decls into headers, include them. | low |

_Non-findings (verified — do NOT "fix"): the `(void)param;` casts at spim.c:164/195/852 sit on externally-fixed signatures (callback/`signal`/readline) — API-mandated unused params; and the `(void)run_program(...)`/`(void)mem_reference(...)` discards of `[[nodiscard]]` returns are deliberate with comments._

**Next step:** go-ahead on the table above, then fix in small mechanical commits, `meson test` + the
`make image` sanitizer gate as the verification gate. Recommend the HIGH `fatal_error` fix land first,
on its own.
