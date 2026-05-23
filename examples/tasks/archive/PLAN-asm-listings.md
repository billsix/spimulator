# Plan: keep the compiler-generated .s on disk for every C demo

## Goal

Every C demo's `.c` should produce a viewable `.s` file
(compiler-generated assembly listing for the build host's
native arch) alongside its `.o` file in the build directory.
Students can open the `.s` to see exactly how the compiler
translated the C — and compare it side-by-side with the
hand-written `.asm` for spim.

Concretely: after `meson compile -C builddir`, the build tree
should contain something like

```
builddir/
  echo
  echo.p/
    19-echo_19-echo.c.o
    19-echo_19-echo.c.s    <-- new, readable, ABI register names, no debug noise
```

(Or in a sibling tree — exact path TBD; see "Open questions"
below.)

## Why

The pedagogy of this repo is "one program, three vocabularies":

1. The freestanding C version — what the student wrote.
2. The hand-written MIPS `.asm` — the lesson, paired with the C.
3. **The compiler's translation of the C to native asm** — what
   a real compiler would emit on the student's actual host.

(3) is implicit today: the student would have to know to run
`cc -S` themselves, with the right flags, in the right
directory.  Most don't.  Making this an artifact of the normal
build means a student can `less builddir/echo.p/echo.c.s`
and learn from it without leaving the workflow.

This also pairs naturally with [`PLAN-build-matrix.md`](PLAN-build-matrix.md),
which proposes cross-compiling each demo to all 5 ISAs at image
build time.  This plan is the **native** version of that — a
much simpler first step that establishes the .s-as-artifact
pattern.

## State of the world today

- meson invokes `cc -c file.c -o file.o`.  No `.s` files end up
  on disk.
- `gcc -save-temps=obj` would emit `.i` (preprocessed) and `.s`
  alongside each `.o` automatically — meson doesn't add that
  flag.
- meson supports a per-language `compile_to_assembly()` method
  on `compiler` objects in newer versions, but it returns a
  generator not a build artifact — usable in `custom_target`
  but not automatic.

## Three implementation options

### Option A — `-save-temps=obj` flag

Add `add_project_arguments('-save-temps=obj', language: 'c')` to
`src/meson.build`.

**Pros:**
- One-line change.
- Standard compiler feature, works with both gcc and clang.
- `.s` lands right next to `.o` in `<demo>.p/`; no path
  surprises.
- Free side effect: `.i` (preprocessed) also lands, useful for
  debugging macro expansion.

**Cons:**
- Small disk cost (~few KB per demo × 40 demos = ~few hundred
  KB).  Negligible.
- The `.s` includes compiler-internal labels (`.LC0`, `.L2`,
  etc.) and debug-info directives if `-g` is on.  Readable but
  noisy.  Can be cleaned with `-fno-asynchronous-unwind-tables`
  and friends (already used in the freestanding builds — so
  the noise is already minimized).
- Some build tools complain about `.s`/`.i` not being declared
  outputs; meson handles it because it's a compiler-driver
  side-effect, not a build rule.

### Option B — custom_target per demo

Define a meson `custom_target` per `.c` that runs
`cc -S -o <demo>.s …`, then a second target that compiles the
`.s` to a `.o`.  Wire those into the existing executable
target.

**Pros:**
- Full control over .s location, naming, flags.
- The .s becomes a first-class build artifact (declared, tracked,
  can be installed, can be referenced by name in tests).
- Could land .s files in a dedicated `builddir/asm-listings/`
  subtree that's easy to point students at.

**Cons:**
- Substantial meson.build changes — every demo's build line
  becomes two targets instead of one.
- Two compile invocations per demo (slower clean rebuilds,
  though small).
- The two-step .c → .s → .o split can mask compile errors
  (.s phase succeeds, .o phase fails on something the .s phase
  should have caught) — rare, but worth knowing.

### Option C — separate target for asm listings

Keep the normal build as-is.  Add a meson `alias_target` or a
single `run_target('asm-listings', …)` that, when explicitly
invoked, regenerates all the .s files in a tidy subtree.

```sh
meson compile -C builddir asm-listings
```

**Pros:**
- Doesn't slow the normal build.
- Listings live in a dedicated subtree the student can
  navigate.
- Easy to teach: "to see the assembly, run this command."

**Cons:**
- Listings can go stale relative to the source unless the
  student remembers to rerun the target.
- One more thing for the student to remember.

## Recommendation

**Option A** as the default, with a one-line meson change.
The cost is trivial, the listings always match the source,
and `<demo>.p/<demo>.c.s` is easy to teach as "look in the
builddir."

If the noise turns out to be too much for students (debug
directives, internal labels), reconsider Option B with
hand-tuned flags.

Option C is appealing for the cross-arch listings (the
PLAN-build-matrix work) where regenerating 5 listings per demo
on every build is genuinely expensive.

## Compiler flags to consider

The freestanding builds already pass these (in `src/meson.build`):

- `-nostdlib`, `-ffreestanding`
- `-fno-asynchronous-unwind-tables`, `-fno-unwind-tables`
- `-fno-stack-protector`, `-fomit-frame-pointer`

For maximum-readable .s, also consider:

- `-fverbose-asm` — annotates each instruction with the C-level
  source variable name where determinable.  **Strongly worth
  trying for pedagogy.**  Slight noise increase.
- `-masm=intel` (x86 only) — Intel syntax instead of AT&T.
  Easier for students who came from x86 reference material,
  worse for everyone else.  Skip; AT&T is the GCC default and
  matches what most Linux toolchain reference docs use.
- `-O0` already de facto — none of the demos pass `-O`.  Good;
  optimized .s for "hello world" is sometimes shorter than the
  source, which is fun but obscures the lesson.
- `-fno-pic` — produces simpler addresses (no GOT indirection
  for global data).  Probably already implied by `-static
  -nostdlib`, worth verifying once.

## Demo placement

Two routes:

- **Inline in src/meson.build** — the same loop that defines
  each demo target also adds the .s emit.
- **Separate src/asm-listings.build** subincluded from the main
  build — keeps the demo loop clean.

Inline is simpler if Option A is chosen (it's a single flag);
separate is cleaner if Option B is chosen.

## Test plan

1. Pick a representative demo (say helloworld).  Apply Option A.
   Verify `.s` lands in `builddir/helloworld.p/`.
2. `less` the .s file — confirm readability.  Look for noise:
   are there too many `.cfi_*` directives?  Are labels
   over-mangled?
3. Try `-fverbose-asm` and compare.  Decide whether the
   variable-name annotations are worth the noise.
4. Roll out to all demos.
5. Add a short README pointer (in `src/` or
   `READING-ORDER.md`) telling students where to look.
6. (Stretch) Add a `meson test` that diffs each `.s` against a
   golden copy — would catch unexpected codegen changes when
   the compiler updates.  Marginal; skip unless someone asks.

## Order of work

1. Apply Option A to one demo's build line — verify behavior.
2. Apply to all demos via project-wide flag.
3. Document in `READING-ORDER.md` ("To compare against the
   compiler's translation, see `builddir/<demo>.p/<demo>.c.s`").
4. Decide on `-fverbose-asm`.  If yes, add it.

Total work: probably 30 minutes including verification, if
Option A.  Hours if Option B.

## Open questions

- **Should the .s files be checked into git?**  No — they're
  build artifacts, and they'd churn whenever the host compiler
  version changes.  Students regenerate them locally.
- **Should the path be visible in `READING-ORDER.md`?**  Yes,
  briefly — one line so the student knows where to look.
- **Cross-arch listings.**  Out of scope here; that's
  PLAN-build-matrix.  This plan establishes the pattern
  natively first.
- **Pre-existing `__asm__()` blocks in the demos (the inline
  `_start` from crt0.h).**  The .s output will include the
  inline asm verbatim — which is fine and pedagogically useful
  ("see, the `_start` you wrote in inline asm ends up in the
  output unchanged").  No special handling needed.

## Out of scope

- Cross-compile to .s for every arch (that's PLAN-build-matrix).
- Disassembly of the final ELF (`objdump -d builddir/<demo>`).
  Cheaper, but shows the linker's view (relocated, addresses
  fixed), not the compiler's.  Could be added as a separate
  artifact later if useful.
- Annotating the .s with the original C source line by line.
  `cc -S -g -fverbose-asm` gets close.  More-elaborate
  source/asm interleaving (like `objdump -S` does) is post-link
  and belongs with the disassembly artifact, not the .s
  artifact.

## Status

Landed 2026-05-23.  Option A (`-save-temps=obj`) added to
`edu_args` in `examples/src/meson.build`, alongside
`-fverbose-asm` for C-source-variable annotations.

After `meson compile -C builddir` from /spimulator root, every
example demo's compiler-generated assembly lands at
`builddir/examples/src/<demo>.p/<munged>.c.s` — and the
preprocessed `.i` lands beside it as a free side effect.

Spot-checked: helloworld.c.s reads cleanly with source-line
comments (`# .../helloworld.c:32:   print_string(...)`) and
register-comment variable names (`#, status`).  Some
`.cfi_*` debug-info noise remains (from the `--buildtype=debug`
setup); skippable but worth a follow-up if it bothers
students — would need `-g0` scoped to examples only.

29/29 tests still pass after the change.
