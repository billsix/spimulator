# Mini C compiler for the example programs (capstone)

**Status:** proposed — investigate first, then plan, then build
**Created:** 2026-07-07

## Request (Bill, 2026-07-07)

A small, simple C compiler for our example source code, as the **culmination
of the student's work**. Two stages:

1. **Stage 1 — compiler in SPIM assembly.** A `.asm` program (running under
   spimulator) that "compiles" C source to SPIM assembly.
2. **Stage 2 — self-host.** Convert the compiler to C (keeping the original
   asm source), such that the asm compiler can compile the C compiler — ending
   with a C compiler, written in C, running on spim.

Explicit non-goals, per Bill:
- **Not full C** — it only has to handle our C example programs.
- **No library requirement** — the stdlib doesn't have to work through it.
- **No real malloc/free** — the compiler may allocate via the break/sbrk
  syscall (spim syscall 9) and simply **leak**; reclamation is optional
  forever.

## Design principles (Bill, 2026-07-07 — these override everything below)

**The #1 priority is understandability for an undergraduate.** Simplicity
beats generality, performance, and even C fidelity. Concretely authorized:

- **Syntax-directed translation** — emit MIPS as you parse, no AST, no IR,
  no passes. (Pedagogically resonant here: spimulator's own assembler
  parser was SDT for years, and the archived Phase-5 docs describe that
  style. The compiler becomes the same shape the student has already seen.)
- **Declarations up front, old-style** — every variable declared at the top
  of its function (C89/K&R discipline, no mid-block declarations). This
  makes the stack-frame layout computable the moment the body starts —
  the frame diagram in the demo headers *is* the symbol table the compiler
  builds.
- **Define the subset, then make the examples conform.** We control both
  sides: specify a simple C subset ("SpimC") that is sufficient for the
  example programs, accept **only** that subset in the compiler (anything
  else is a clean, line-numbered error), and **rewrite the example C to the
  subset style** where it currently strays (mid-block declarations,
  `for`-loop initializers with declarations, etc.). The paired-golden test
  harness proves each rewrite preserves behavior before the compiler ever
  sees it.
- Any other simplification that buys clarity is on the table: restricting
  expression forms, requiring braces, one function per `.text` block —
  "we can do anything."

This principle also settles the Route A/B question's spirit: whichever
route, the artifact students read must be small enough to read whole.

## Investigation step 1: what C do the examples actually use?

The compiler's required surface = the constructs in `examples/src/**/*.c`.
Initial census (2026-07-07, grep over the tree — the investigate phase should
turn this into a precise per-construct inventory with file lists):

| Construct | Files using it | v1 verdict (proposed) |
|---|---|---|
| `struct` / `typedef` / `enum` / `goto` | **0** | omit entirely |
| `switch` | 1 | identify the file; rewrite it as if/else or defer |
| `float`/`double` | 1 | identify; exclude from scope (spim FP exists but skip) |
| function pointers | libstdlib demos only (`bsearch` cmp, `atexit`) | exclude from v1; revisit for self-hosting needs |
| `int`/`char`/`unsigned`, pointers, arrays | pervasive | required |
| `if`/`while`/`for`, `&&`/`||`/`!`, comparisons, arithmetic incl. `%` | pervasive | required |
| functions, recursion (fibonacci/hanoi/queens), `static` (27 files) | pervasive | required |
| string literals, char literals, `\n` escapes | pervasive | required |
| preprocessor | `#include "io.h"/"os.h"/"crt0.h"` + `#if` arch blocks in crt0/os | see below — sidestep, don't implement |

**The preprocessor/inline-asm question resolves itself for the spim target:**
`os.h`/`crt0.h` exist to reach *Linux* syscalls via GNU inline asm. When the
target is spim, the io surface (`print_string`, `print_int`, `read_char`,
`os_read`, `os_write`, …) is already implemented as hand-written MIPS in the
demo/library tree. So the compiler treats those functions as **externs** and
the output links against the existing asm library via the (working)
multi-file load — no preprocessor, no inline asm, no libc. The compiler
compiles the `my_main`-and-helpers portion of each demo only. This ties
directly into [`multi-file-load.md`](multi-file-load.md)'s shared-library
direction: the library is the runtime, the mini-compiler emits calls into it.

Investigation must also decide the **input convention**: real `.c` files have
the `#include` lines and arch `#if` blocks — simplest is a pre-pass rule
("strip `#`-lines; treat io/os functions as known externs"), so the student
compiles the same files they've been reading all along.

## Investigation step 2: how big is this, honestly?

A recursive-descent compiler for the subset above (ints/chars/pointers/
arrays, full statement set, precedence climbing for expressions, no
optimizer, everything on the stack, spill-everywhere codegen) is roughly a
**few-thousand-line C program** — c4/chibicc-subset territory. Writing that
*directly in MIPS assembly* is the multi-month version. So evaluate two
routes in the plan phase:

- **Route A (as requested, literal):** write the compiler in SPIM asm first,
  then port to C.
- **Route B (same endpoint, curriculum-consistent):** write the compiler in
  the C subset first (it must be written in the subset it compiles — that's
  the self-hosting constraint either way), verify it with clang natively,
  then produce the asm version — either by hand-translating it (the
  curriculum's own C→asm methodology, applied to the biggest program in the
  tree) or, cutely, by compiling it with itself once a native build exists.
  The "original asm source" Bill wants still exists and is kept; it's just
  written second, from a known-good reference.

Route B is dramatically lower-risk for the same deliverables; the plan phase
should put effort estimates on both and let Bill pick.

## Compiler design constraints (v1)

- Single pass, syntax-directed translation to asm text (per the design
  principles above — no AST/IR), writable in the subset itself
  (self-hosting rules out anything fancy anyway).
- Memory: bump allocator over sbrk (syscall 9); no free, leaks by design.
- Codegen: naive and readable — every local on the stack frame, load/operate/
  store per expression node; correctness and legibility over quality. The
  output should look like the hand-written demos, not like `-O2`.
- Diagnostics: line-numbered error + exit nonzero is enough.
- I/O: read source via spim syscalls (13/14), write the `.s` to stdout or a
  file — the compiler is itself just another demo program, the biggest one.

## Acceptance / testing

The existing harness is the perfect gate: for each in-scope demo, compile
`<demo>.c` with the mini compiler → run the produced asm under spimulator →
**diff against the same goldens** `examples/tests/run-demo.sh` already pins.
Start with helloworld → print1through10 → the loop demos → recursion demos,
growing the construct coverage demo by demo in curriculum order.

Self-hosting acceptance (stage 2): asm-compiler compiles the C compiler; the
resulting compiler compiles the demo set with identical output to stage 1.

## Order of work

1. Investigate: precise construct inventory of `examples/src/**/*.c` (incl.
   naming the one `switch` and one `float` file), pick Route A or B, and
   **write the SpimC subset definition** (grammar + the declarations-first
   rule + what's excluded), sized against the inventory. Write it up here
   for a go-ahead.
2. **Subset-conformance pass over the example C**: rewrite the demos into
   SpimC style (declarations hoisted to function tops, excluded constructs
   replaced), one demo per commit, goldens proving behavior unchanged.
   This lands value even before the compiler exists — the C reads more
   uniformly against its asm — and every rewritten demo is a ready-made
   compiler test case.
3. Build the compiler to pass helloworld end-to-end through the golden
   harness; then widen construct-by-construct in curriculum order.
4. Stage-2 self-host per the chosen route (the compiler source itself must
   be SpimC from day one).

## Relation to other tasks

- [`multi-file-load.md`](multi-file-load.md) / [`libstr.md`](libstr.md) —
  provides the runtime-library linking model the compiler's output relies on.
- [`timing-model.md`](timing-model.md) — a compiled-vs-hand-written demo
  comparison becomes a nice performance lesson once both exist.
- `pgu/` — the book builds up exactly the C→asm mental model the student
  needs before attempting this; this task is the capstone after it.
