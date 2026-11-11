# spimulator — inherited idiosyncrasies still open

**Reference document.** The parts of the 2026-08-25 code-idiosyncrasy audit that are still true. The
audit's full itemized table is in `tasks/archive/2026/08/27/code-idiosyncrasies-audit.md`; its
correctness fixes (the `fatal_error` format-string bug, `dump-opcodes.c`'s implicit-int `main`, the
`int` sizes of `xmalloc`/`zmalloc`, the dead `kernel` parameter of `emit_dir_seg`) were made in
`tasks/archive/2026/10/16/c23-modernization-pass2.md`, Tier 1.

## Still open

- **Comment/code drift** in `examples/src` and `pgu/src` asm and comments: comments that describe
  behavior the code no longer has.
- **Copy-paste residue in hand-written `.asm`** (odd register choices, dead stores, leftovers from a
  neighboring demo), found only by reading, since asm has no tooling.
- **Cosmetic demo-name off-by-ones.** They need the maintainer's direction, so the mechanical fix task
  left them out.

## What's clean (so nobody re-audits it)

In the demo and book layers (`examples/src`, `pgu/src`): `argc`/`argv` are used legitimately (one
`(void)argc;` in `pgu/src/c/toupper-nomm-simplified.c` is deliberate), byte buffers are already
`unsigned char`/`int8_t`, and there are no implicit-int definitions, no `register` and no `char*`
byte buffers. The emitter naming (the `_inst` suffix) was settled, and the `include/spim.h` split
declined, in `tasks/archive/2026/11/11/codebase-cleanup-plan.md`.

## Cross-links

- `tasks/reference/parser-ast-emit.md` (the parser/emit pipeline).
- `tasks/archive/2026/07/07/opcode-types-descriptive-names.md` (the opcode-table naming).
