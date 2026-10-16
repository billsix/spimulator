# Example C is written for C→asm legibility, not modern-C style

**Reference document** — read before modernizing, reformatting, or "cleaning up" any C under
`examples/` or `pgu/`, and before writing a new paired demo. States a deliberate opt-out, not an
oversight. Update in place. (Written 2026-10-16 — William Emerison Six <billsix@gmail.com> — when
the simulator-core modernization was scoped to `src/`+`include/` only.)

## The rule

The teaching C — every `examples/src/**/*.c` and the C under `pgu/` — is a **paired C + MIPS-asm
demo**: a student reads the `.c` next to the compiler's generated assembly (and next to the
hand-written `.asm` port) to learn how a high-level construct becomes machine code. That purpose,
not modern-C style, is the contract. **Do not apply the shared C coding standard
(`~/.claude/reference/c-coding-standard.md`) to this code.** Modernization is scoped to the
simulator core (`src/`+`include/`).

The correctness bar here is the examples test suite (`examples/tests/run-demo.sh`, which diffs both
the native C binary and the asm-under-spim against a pinned golden). The *style* bar is: a student
can trace each line of C to the asm it produces.

## Why — "good modern C" and "legible codegen" pull apart

Most idioms the standard calls good actively obscure the C→asm mapping a demo exists to show:

- **Declare all locals at the top of the function** — do NOT declare-at-first-use here. A block of
  declarations at the top mirrors how the compiler reserves the stack frame in the prologue, so a
  student can line each local up with its frame slot (`addi $sp, $sp, -N` and the offsets off `$sp`).
  Declare-at-first-use scatters the frame across the body and breaks that correspondence. This is the
  canonical case and the reason this doc exists.
- **Keep the control flow flat and explicit** — a plain `for`/`while`/`if` maps to visible
  branches and labels. Helper extraction, early-return restructuring, and clever expressions move or
  hide the codegen the demo is tracing.
- **Avoid idioms with no obvious asm shadow** — heavy `const`, designated / compound-literal
  initializers, `typeof`/`auto`, and `_Generic` either compile away or complicate the mapping without
  teaching anything about the machine. Leave them out.
- **Match the asm port and the native build.** The `.c` and its `.asm` sibling are meant to read as
  one lesson, and the C is also compiled freestanding (`-nostdlib`, `-O0`, own `_start`); keep the C
  simple enough that `-O0` output stays a faithful line-by-line image of the source.

This is the `c-modernization-sweep.md` principle ("opt out in place where the obvious fix destroys
something valuable") applied at the whole-subtree level: the valuable thing is the C→asm pedagogy.

## What still applies

Legibility is not an excuse for defects. Real bugs, UB the sanitizer gate would catch, and
comment/code drift are still worth fixing — but behavior- and shape-preservingly, the same way
`c-modernization-sweep.md` treats a deliberate teaching artifact. The inherited-idiosyncrasies audit
already found the demo/book layers clean (`tasks/reference/inherited-idiosyncrasies.md`: argc/argv
legitimate, byte buffers already unsigned, no K&R residue), so there is little to do here anyway.

## Cross-links

- `~/.claude/reference/c-coding-standard.md` — the standard this subtree is exempt from (applies to
  `src/`+`include/`).
- `~/.claude/reference/c-modernization-sweep.md` — the opt-out-in-place principle this specializes.
- `tasks/reference/mips-fpu-and-float-demos.md` — how to author a floating-point demo (the C↔asm
  byte-matching technique).
- `examples/READING-ORDER.md`, `examples/TEACHING-ASSEMBLER-INTERNALS.md` — the student-facing
  curriculum and the assembler-internals tour.
