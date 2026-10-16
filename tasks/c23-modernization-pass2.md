# Modernize the simulator core to modern C — `src/` + `include/`

**Status:** COMPLETE (2026-10-16)
**Priority:** 5 · **Difficulty:** 7
**Created:** 2026-06-16 · **Rescoped & executed:** 2026-10-16 (William Emerison Six <billsix@gmail.com>)

## BLUF

Brought the simulator core (`src/` + `include/`) up to the shared modern-C bar
(`~/.claude/reference/c-coding-standard.md`), modelled on the APUE tree's modernization
(`/foo/opt/apue`, `2026/10/16/modernize-to-modern-c.md`), preserving runtime behavior — the
`ast_parity`/`ast_parity_all` byte-identical memory goldens plus the full `meson test` suite and the
ASan/UBSan sanitizer gates are the oracle. The delta here was **small on purpose**: the archived pass 1
(`tasks/archive/2026/05/22/c23-modernization.md`) and the parser/AST work had already landed most of the
standard (`nullptr`, `constexpr`, `enum : T`, attributes, `static_assert`, `<stdbit.h>`/`<stdckdint.h>`,
`#embed`, `typeof` macros, and — verified during this pass — declare-at-first-use + initialize + `const`
throughout). What this pass added: the folded-in correctness fixes, a portable `-Werror` gate,
fixed-width types for the modelled instruction fields, and `snprintf` for the two unbounded builders.
The one large remaining item — tightening the signed/unsigned integer handling that *is* the MIPS ALU
semantics — was **split to its own task** because it is unsafe as a mechanical sweep.

## Scope (as executed)

- **IN: `src/` + `include/`.** **OUT: `examples/` + `pgu/` C** (teaching code read against generated
  asm — `tasks/reference/example-c-teaching-style.md`); **OUT: the `opcodes.h` OP X-macro redesign**
  (`tasks/opcodes-x-macro-redesign.md`); **OUT: `tree-sitter/` generated C**.
- `gnu23` kept (the `MIN`/`MAX`/`ROUND_*` statement-expression macros need it); `-Werror` added.

## What was done (one commit per logical unit; see git log for the detail)

1. **Correctness fixes** (folded in from `fix-inherited-idiosyncrasies.md`, now superseded):
   - `src/spim.c` `fatal_error` **HIGH bug** — removed the `fmt = va_arg(args, char*)` line that
     overwrote the format string (broke all 14 error paths), added `va_end`. Verified: `%s` diagnostics
     now expand their argument instead of mis-using it as the format.
   - `src/dump-opcodes.c` implicit-int `main` → `int main(void)`; `src/spim-utils.c`
     `xmalloc`/`zmalloc` `int` → `size_t` (and the OOM message's dangling `%d` → `%zu` + the size);
     `src/parser.c` dead `kernel` parameter dropped from `emit_dir_seg`.
2. **`-Werror` gate** — scoped to the `spimulator` core target via `-Dcore_werror` (default true, so
   the out-of-scope `examples/` keep their own warning posture). The warning flags are now probed with
   `cc.has_argument()` so the build is clean under **both** the image's clang 22 and a host gcc 16;
   clang gets plain `-Wimplicit-fallthrough` (gcc keeps level 5) and `-Wno-string-concatenation` for
   explain.c's deliberate multi-line teaching descriptions.
3. **Fixed-width machine fields** — `include/instruction.h` `mips_instruction`/`imm_expr` fields:
   `short`→`int16_t`, `unsigned char`→`uint8_t`, `int offset`→`int32_t` (representation-identical, so
   `ast_parity` is byte-identical).
4. **`snprintf`** over the two `sprintf` builders in `src/pseudo-op.c` (`sizeof`-bounded).

## Deferred (its own task) — integer-conversion hardening

A `-Wconversion -Wsign-conversion` probe found 371 core warnings, 358 of them `-Wsign-conversion` and
326 in `src/run.c`, where the signed/unsigned casts *are* the MIPS semantics (add/addu, sra/srl,
branch-offset sign-extension), plus the `symbol-table.h` `long addr` field that doubles as a signed
constant store. Making the core clean under those flags and adding them to the gate needs
per-instruction review and UBSan coordination, not a blind sweep — filed as
`tasks/integer-conversion-hardening.md` (proposed).

## Standard-coverage audit (`~/.claude/reference/c-coding-standard.md`)

| Item | Status | Note |
| --- | --- | --- |
| declare-at-first-use + initialize | done (pass 1) | verified present across the core |
| one declaration per line | done (pass 1) | |
| `const` by default | done (pass 1) | e.g. `dup_imm(const imm_expr*)` |
| fixed-width / domain types for machine state | **done (structural)** | instruction.h fields; deep sweep → integer-conversion-hardening |
| `size_t` for host indices | done | xmalloc/zmalloc; rest tracked in the integer task |
| `bool`/`true`/`false` | done (pass 1) | |
| integer-conversion (sign-compare / `-Wconversion`) | **deferred** | integer-conversion-hardening.md |
| one owner / one free; check allocations | done | fatal_error/xmalloc paths corrected |
| `snprintf` over `sprintf`/`strcpy`/`strcat` | **done** | pseudo-op.c; no others in core |
| designated / `{ }` initializers | n/a | core zero-inits are dynamic (xmalloc+memset), not stack aggregates |
| `unreachable()` | declined (reviewed) | core `default:` cases are reachable on bad binary input (as pass 1 found) |
| binary literals / digit separators | declined | cosmetic; the opcode table is out of scope |
| `nullptr`, `constexpr`, `enum : T`, `static_assert`, `#embed`, `<stdbit.h>` | done (pass 1) | |
| `[[noreturn]]`/`[[nodiscard]]`/fallthrough | done (pass 1) | |
| `-Werror` gate | **done** | core-scoped, portable gcc + clang |

## Oracle (final)

gcc 16 core build clean under `-Werror`, `meson test` **36/36**; clang 22 core build clean under
`-Werror`; the UBSan-trap and ASan gates (clang, `spimulator` target, regression suite) **25/25** —
matching the pre-work baseline (`594e06b`). Verified standalone with the Dockerfile's own gate recipe.

## Cross-links

- `~/.claude/reference/c-coding-standard.md`, `~/.claude/reference/c-modernization-sweep.md`,
  `~/.claude/reference/porting-with-an-output-oracle.md`.
- `tasks/integer-conversion-hardening.md` (the deferred integer work),
  `tasks/reference/inherited-idiosyncrasies.md` (the correctness findings, now fixed),
  `tasks/reference/example-c-teaching-style.md` (why examples/pgu are out),
  `tasks/opcodes-x-macro-redesign.md` (the OP macro, out of scope).
- `tasks/archive/2026/05/22/c23-modernization.md` (pass 1).
