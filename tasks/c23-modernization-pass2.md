# C23 modernization, pass 2 — simulator + example/pgu subprojects

**Status:** proposed — needs go-ahead
**Created:** 2026-06-16

## Context / what's already done

A first C23 pass is archived at `tasks/archive/2026/05/22/c23-modernization.md`.
Today the simulator builds as **`c_std=gnu23`** (`meson.build:3` `default_options`),
and `src/`/`include/` already use some C23: `nullptr` throughout, `typeof` (in the
`MIN/MAX/ROUND` statement-expression macros in `spim.h`), builtin `bool`. So this
is a *second* pass: (1) decide whether to move from **gnu23 → strict c23**, (2)
adopt more C23 where it improves clarity/safety, and (3) bring the **example
projects** (`examples/`) and the **`pgu/`** book port up to the same bar.

This pairs naturally with the UBSan sweep (done —
`tasks/archive/2026/06/16/ubsan-sweep.md`); several C23 features (fixed-width
`_BitInt`, explicit `unsigned` MIPS-word handling) touch the same integer-UB
surface it mapped.

**Dedup note (2026-07-07):** the codebase-cleanup Tier D pass (archived at
`tasks/archive/2026/05/22/c23-modernization.md`, summarized in
`codebase-cleanup-plan.md` §Tier D) already delivered several items this doc
originally listed as candidates: `constexpr` (~85 constants), `enum E : T` on
every named enum, `[[nodiscard]]`/`[[noreturn]]`, `static_assert`,
`<stdbool.h>` removal, `<stdbit.h>`/`<stdckdint.h>`, and `#embed` for the
exception handler. Those are struck below; what's actually left for pass 2
is the gnu23→c23 decision, `unreachable()`, binary literals / digit
separators, `_BitInt` evaluation, and extending the bar to `examples/`+`pgu/`.

## Investigate (the "what could be better" audit)

For each candidate, decide adopt / skip / defer, with a one-line rationale:

- **gnu23 → c23.** Inventory GNU-only constructs (`__extension__` statement
  expressions in `spim.h` macros, any `__attribute__`, `({ })`, `,##__VA_ARGS__`,
  inline asm in examples). C23 standardizes `typeof`; statement-expressions are
  still GNU. Decide: keep `gnu23` (pragmatic) vs. `c23` + replace extensions
  (portability win). Likely keep gnu23 for the freestanding examples (need inline
  asm syscalls), evaluate strict c23 for `src/`.
- ~~`constexpr`~~ — done in Tier D (src/); still to check in examples/pgu.
- ~~`enum` with fixed underlying type~~ — done in Tier D (every named enum).
- ~~Attributes~~ — `[[nodiscard]]`/`[[noreturn]]` done in Tier D;
  `-Wimplicit-fallthrough=5` polices the `run.c` switch. Nothing left here.
- ~~`static_assert`~~ — in place (`include/memory.h:19` and Tier D).
- **`unreachable()`** for the `default:` of exhaustive opcode switches.
- ~~`#embed`~~ — done in Tier D (default exception handler is embedded).
- ~~Drop `<stdbool.h>`/`<stddef.h>` shims~~ — done.
- **Binary literals + digit separators** (`0b…`, `0xFFFF'0000`) for the opcode and
  mask tables — readability.
- **`auto`** — likely *skip* in teaching/sim code (hurts readability); note it.
- **`_BitInt(N)`** — evaluate for exact MIPS field widths; probably overkill.

## Scope decisions to make up front

- **`src/` (simulator):** primary target — safe to modernize aggressively.
- **`examples/` (curriculum C):** these are `-nostdlib`, `-O0`, deliberately
  *simple* C that students read alongside the compiler's MIPS output. Adopt only
  C23 that keeps the codegen legible and the source approachable (`constexpr`,
  `static_assert`, attributes, `nullptr`/`bool`) — **skip** anything that obscures
  the C→asm mapping. Keep their inline-asm syscalls (needs gnu23).
- **`pgu/`** book port: check whether it ships C at all; align its `c_std` if so.
- The `tree-sitter/` grammar's generated C is third-party — out of scope.

## Plan

1. **Audit & decide** — produce the adopt/skip table above; settle gnu23-vs-c23
   per subtree. Record decisions in this doc.
2. **Simulator pass** — apply chosen features to `src/`+`include/`, smallest
   logical commits (attributes → constexpr/enums → static_asserts → cleanup).
   Keep behavior identical; lean on the existing `meson test` suite (29/29) after
   each step.
3. **Examples pass** — apply the conservative subset; confirm the demos still
   build, their `.s` listings stay clean, and the `examples` test suite passes.
4. **pgu pass** (if it has C) — align std + any cheap wins.
5. **Tighten the build** — set the agreed `c_std`, raise `warning_level`/add
   `-Wc23-extensions` or `-Wgnu` to police drift; consider failing the image build
   on new warnings (consistent with the suite already gating the image).

## Constraints / notes

- In-container only (per the working arrangement); verify via `make image`
  (builds + runs the full suite) after each subtree.
- Don't regress the teaching value of `examples/` — pedagogy beats feature-count.
- Clang in the image is **clang 22** (full C23); fine. If GCC is ever a target,
  re-check feature availability.
- Coordinate with `tasks/ubsan-sweep.md`: do the integer-type tightening once,
  not twice.

## Acceptance

- Decisions table filled in (adopt/skip per feature, per subtree).
- `src/` (and examples/pgu as scoped) build under the agreed `c_std` with the
  full `meson test` suite green; `make image` green.
- Build policed against regressions (warning flags / std pinned).
