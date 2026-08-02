# Shared library .asm files for the demos (multi-file load follow-on)

**Status:** re-scoped 2026-07-07 — the loading mechanism turned out to already
be shipped; what remains is the library work built on top of it.
**Priority:** 5
**Difficulty:** 5
**Created:** 2026-07-07

## Original question — resolved: multi-file loading already works

Bill asked whether spimulator can load two assembly files into one program
(one library, one main), and if not, whether to add it via preprocessor,
runtime support, or a fake-"linker" tool.

**It already works, both ways, and is regression-tested:**

- Repeated CLI flags: `spimulator -f lib.s -f main.s` assembles both into the
  shared segments; a `jal` from `main.s` into a label defined in `lib.s`
  resolves and runs (verified 2026-07-07 with a two-file probe; exit path and
  output correct).
- Sequential REPL `load "lib.s"` then `load "main.s"` behaves the same
  (additive; `reinitialize` is the reset).
- `tests/tt.multifile.s` + `tests/tt.multifile.helper.s` pin the behavior in
  the regression suite.

So no preprocessor, runtime change, or fake-linker tool is needed. (A
fake-linker could still exist someday as a *teaching* artifact — making "the
linker" a visible step — but nothing functional depends on it; dropped from
scope.)

## Remaining scope: a real library file the demos share

With loading in place (Bill, 2026-07-07): port the standard musl libc
functions **not already implemented** as one large library `.asm` (+ paired
C), and **extract the patterns already duplicated in existing demos** so demos
`jal` into the shared library instead of carrying private copies.

- Already done: `examples/src/lib/libctype`, `libstdlib` (musl-adapted, C side).
- First tranche, already specced: [`libstr.md`](libstr.md) — 10 string/memory
  functions. That task is now **unblocked** and its planned two-file
  invocation (`spimulator -f libstr.asm -f str-demo.asm`) works today.
- Then: survey what the demos reinvent
  (`grep -rn '^atoi:\|^str_eq:\|^print_uint:' examples/src --include='*.asm'`),
  promote those routines into the library, and de-duplicate the demos to
  `jal` the shared copies.
- Constraints as ever: spim's 17-syscall surface (nothing malloc-dependent
  unless built on sbrk), naive readable implementations over musl's optimized
  ones, and don't regress the demos' teaching value — a demo whose whole
  lesson *is* the subroutine (e.g. factorial's atoi walkthrough) may keep its
  private copy deliberately; decide per demo.

## Ordering

1. `libstr.md` (library #2 of the libctype → libstr → libstdlib sequence) —
   the port itself.
2. The duplication survey + de-dup sweep across existing demos.
3. Any further musl tranches (strstr/strspn family, etc.) as wanted.

## Open questions

- Label visibility: today all labels are global across files (SPIM tradition).
  Honoring `.globl` for export with file-local non-globals would be a more
  realistic linking lesson — worth a separate proposal if the de-dup sweep
  makes label collisions annoying.
- Where library `.asm` files live for invocation ergonomics: students typing
  two `-f` paths per run may want a `%lib` search path or a Make target
  wrapping it. Decide during the libstr demo work.
