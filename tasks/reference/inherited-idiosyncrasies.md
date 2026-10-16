# spimulator — inherited idiosyncrasies & latent bugs (verified)

**Reference document** — durable findings from the 2026-08-25 code-idiosyncrasy audit (harvested here so
they survive the task's archival). Every item was `file:line`-verified by direct inspection; the HIGH bug
was independently re-confirmed 2026-08-27. Not a task; update in place as items are fixed. The original
full itemized table lives in the archived audit
(`tasks/archive/.../code-idiosyncrasies-audit.md`).

## Summary

The **demo/book layers** (`examples/src`, `pgu/src`) are in good shape — `argc`/`argv` handled
legitimately, byte buffers already `unsigned char`/`int8_t`, no K&R residue. The **simulator core**
(`src/`, `include/`) is partially-modernized C23 grafted onto legacy SPIM bones, and hides **one genuine
latent bug** plus a scatter of dead/misnamed/mistyped items.

## HIGH — a real latent bug  — FIXED 2026-10-16

- **`src/spim.c:1502-1508` — `fatal_error` corrupts its own format string.** The body does
  `va_start(args, fmt); fmt = va_arg(args, char*); vfprintf(stderr, fmt, args);` — the **`va_arg` line
  overwrites the format string with the first variadic argument.** All 14 call sites break: a bare-string
  call reads a nonexistent vararg as `fmt` (UB); `"…: %d\n", OPCODE(...)` reinterprets an `int` as a
  `char*` format (segfault); `"…: %s\n", filename` prints the filename *as* the format. The correct
  sibling `run_error` (`spim.c:1512`, marked `[[noreturn]]`) has no such line. **FIXED 2026-10-16**
  (c23-modernization-pass2, Tier 1): the `fmt = va_arg(...)` line removed, `va_end(args)` added;
  verified the `%s` diagnostics now expand their argument.

## The "random void argc" shape the maintainer remembered — real (dump-opcodes FIXED 2026-10-16)

- **`src/dump-opcodes.c:61` — `main(int argc, char** argv)` with NO return type** (implicit-int, illegal
  in the C23 the tree targets) and `argc`/`argv` unused. **FIXED 2026-10-16**: `int main(void)`.
- `pgu/src/c/toupper-nomm-simplified.c:89` — a defensible `(void)argc;`.

## Mistyped sizes — FIXED 2026-10-16

- **`src/spim-utils.c` — `xmalloc`/`zmalloc`** took a signed `int` into `malloc`/`calloc`.
  **FIXED**: `size_t` (and the out-of-memory message's dangling `%d` → `%zu` with the size passed).

## Other verified categories (see the archived audit for the full itemized table)

- **Comment/code drift** in `examples/src` and `pgu/src` asm/comments (comments describing behavior the
  code no longer has; the `opcodes.h`/`opcode-types.h` self-references tracked separately in
  `opcode-types-descriptive-names.md`).
- **Copy-paste residue in hand-written `.asm`** (weird register choices, dead stores, leftovers from a
  neighboring demo) — read by eye since asm has no tooling.
- **A few dead/misnamed items**, e.g. a dead `kernel` parameter at `parser.c` `emit_dir_seg` (**FIXED 2026-10-16**); some cosmetic
  demo-name off-by-ones (these need maintainer direction — scoped OUT of the mechanical fix task).

## What's clean (so nobody re-audits it)

Demo/book layers: argc/argv legitimate, byte buffers already unsigned, no implicit-int defs, no
`register`, no `char*` byte buffers. Naming-style consistency is deliberately NOT here — it lives in
`codebase-cleanup-plan.md` Tier B; header hygiene in Tier C.

## Follow-on

All the correctness fixes above **landed 2026-10-16** as Tier 1 of `c23-modernization-pass2`
(`fix-inherited-idiosyncrasies.md` superseded). Cosmetic renames remain excluded (need direction).

## Cross-links

- `tasks/reference/parser-ast-emit.md` (the parser/emit pipeline), `codebase-cleanup-plan.md` (naming/
  header hygiene tiers), `opcode-types-descriptive-names.md` (the opcode-table naming).
