# UBSan sweep — make the codebase clean under `-fsanitize=undefined`

**Status:** DONE (2026-06-16)
**Created:** 2026-06-16

## Primer: what are ASan / UBSan (and friends)?

**Sanitizers** are *correctness checkers built into the compiler* (clang and gcc
both ship them). You turn one on with a `-fsanitize=...` flag at build time; the
compiler then weaves extra instructions into your program that watch for a
specific class of bug *as the program runs*, and report (or trap) the instant one
happens. They are **dynamic** tools: they only catch a bug on a code path that
actually executes with inputs that actually trigger it — so their coverage is
only as good as the tests/inputs you run under them. That's the opposite of a
*static* analyzer (which reads code without running it). The trade is precision:
when a sanitizer fires, it's a real bug, with the exact line — almost no false
positives. The cost is runtime overhead (typically 2–4×) and a bigger binary, so
you use them in **dev/test/CI builds, never in shipped/release builds**.

The two that matter here:

### ASan — AddressSanitizer (`-fsanitize=address`)
Catches **memory-safety** bugs:
- buffer overflow / underflow (reading or writing past the end of an array or
  heap block),
- use-after-free and use-after-return (touching memory that was already freed or
  whose stack frame returned),
- double-free, and (with LeakSanitizer, bundled in) memory leaks.

How it works, briefly: ASan allocates a parallel "shadow memory" that records,
for every byte, whether it's currently legal to touch, and surrounds every
allocation with poisoned **redzones**. Every load/store the compiler emits gets a
shadow check first; touching a redzone or freed byte aborts with a precise report
(address, allocation/free stack traces). **We did *not* find ASan bugs in this
sweep** — spim's issues were all the other kind. (ASan is still worth running
later; it just wasn't the focus.)

### UBSan — UndefinedBehaviorSanitizer (`-fsanitize=undefined`)
Catches **undefined behavior (UB)** — operations the C standard declares have *no
defined meaning*, which the compiler is therefore free to assume "never happen."
This is what this whole task was about. The relevant sub-checks (UBSan is an
umbrella of many) include:
- **signed integer overflow** — `INT_MAX + 1`, `INT_MIN * -1`, negating
  `INT_MIN`. (Unsigned overflow is *defined* to wrap, so it's fine — which is
  exactly why every fix here was "do the math in `unsigned`.")
- **bad shifts** — left-shifting a *negative* signed value, or shifting by a
  count `>=` the type width (`1 << 31` on a 32-bit `int` is UB because it hits
  the sign bit; `x << 32` is UB outright).
- null-pointer deref, misaligned pointer access, out-of-range enum/bool values,
  `int`-vs-pointer mistakes, reaching `unreachable()`, etc.

UBSan has **two output modes**, both used in this task:
- **diagnostic** (`-Db_sanitize=undefined`): links a runtime library that prints
  `file.c:line:col: runtime error: <what happened>` and (by default) keeps
  going. Great for *locating* bugs — but in this project it **under-reported**
  (it only flagged 2 of the ~9 sites; see the Resolution). It also needs that
  runtime `.so` to link and load, which fought us (`_Unwind_*`/`dlsym` link
  errors; fixed with `-shared-libsan -Wl,-rpath,…`).
- **trap** (`-fsanitize-trap=undefined`): instead of calling a runtime, the
  compiler emits an illegal instruction (`ud2`) at each check, so any UB
  instantly kills the process with **SIGILL**. No runtime to link (simpler), and
  it proved *stricter/more reliable* here — but it tells you nothing about
  *where* (no message), so we paired it with `gdb` backtraces to pinpoint each
  site. Trap mode is the better **pass/fail gate**; diagnostic is the better
  **explainer**.

(Other sanitizers you'll hear about, not used here: **TSan** ThreadSanitizer for
data races, **MSan** MemorySanitizer for reads of uninitialized memory, **LSan**
LeakSanitizer for leaks. spim is single-threaded, so TSan isn't relevant.)

### Why UB is dangerous even when the program "works"
Every bug fixed here produced the *correct answer* at `-O0` (the debug build) —
the CPU just happened to do the two's-complement thing we wanted. The danger is
the **optimizer**. At `-O2`/`-O3` the compiler is allowed to assume UB never
occurs and optimize on that assumption: it can delete a bounds check it "proves"
is redundant, assume a loop counter can't overflow and so can't terminate the way
you expect, or compute a branch target wrong. The result is a program that passes
its tests in debug and silently misbehaves in release — the worst kind of bug to
chase. spim is an *emulator*: it does a huge amount of deliberate bit-twiddling
(shifts, masks, two's-complement wraparound) to model MIPS hardware, so it's
unusually rich in these signed-overflow/shift patterns. That's why the sweep
found ~9 of them.

### Why we might add a sanitizer pass to the image
The image build **already runs the full `meson test` suite and fails the build
if any test fails** — that's the project's safety net. A "UBSan lane" would extend
that net: build spim a second time with `-fsanitize-trap=undefined`, run the
suite, and **fail the image if any UB fires**. Benefits:
- **Regressions can't sneak back in.** Today the code is UB-clean; without a gate,
  the next hand-written shift/overflow re-introduces a landmine and nobody
  notices until a release miscompiles.
- **Cheap, given the suite already exists** — it's a second compile + test run.
- **Trap mode needs no extra packages** (no runtime to ship), so it's low-friction
  in the Fedora image.

Costs / caveats to weigh:
- Extra build time (a second instrumented compile of `src/`).
- Must scope it to **spim (`src/`) only** — the `-nostdlib` `examples/` demos
  break if sanitized (no runtime to call), so the lane builds just the
  `spimulator` target.
- It's a *shipped-build/CI policy choice* (do we want every image build to enforce
  this?), which is why the decision was left to Bill rather than wired in here.

## Resolution (2026-06-16)

spim (`src/`) is now UB-clean under `-fsanitize=undefined`. **Key methodology
finding:** diagnostic mode (`-Db_sanitize=undefined`, shared runtime) **under-
reported** — it surfaced only the 2 shift sites — while **trap mode**
(`-fsanitize-trap=undefined`, links with no runtime) caught the rest. So trap
mode was used as the authoritative gate, fixing sites serially (each test aborts
at its first), backed by gdb backtraces (`bt`) to locate each, plus a hand-written
adversarial cover program (`/tmp/ubcover.s`, not committed) hitting every fixed
instruction with high-bit/`INT_MIN`/negative operands.

**Sites fixed (all preserve emulated MIPS semantics — bit patterns identical):**

*Left shift of negative / into the sign bit:*
- `include/instruction.h` `BRANCH_OFFSET` — `IOFFSET << 2` (signed `short`).
- `src/run.c` LUI (`(short)IMM << 16`), SLL+SLLV (`gpr[RT] << shamt`),
  LWL masked shifts (`(word & 0xff..) << n`), SWR shifts (`reg << n`).
- `include/registers.h` `CC_mask` — `1 << 31` for FPU condition-code 7
  (caught only by the `C.LT.D CC=7` path in `tt.core.s`).
- `src/data.c` + `src/instruction.c` — `1 << alignment` for `.align` (made `1u`,
  defensive: latent UB only for `.align ≥ 31`).

*Signed integer overflow (add/sub/negate):*
- `src/run.c` `signed_multiply` — `-v` negation of `INT_MIN`.
- `src/run.c` ADDIU, ADDU, the 18 `gpr[BASE] + IOFFSET` address computations,
  and MADD/MSUB accumulators (`product_low/high`, `tmp` → `u_reg_word`).

**Already clean (verified, not touched):** ADD/ADDI/SUB (already use C23
`ckd_add`/`ckd_sub`), DIV/DIVU (guard div-by-zero and `INT_MIN/-1`), SUBU
(unsigned), MUL/MULT (route through `signed_multiply`), `TARGET << 2` (`mem_addr`
is unsigned), the multiply mid-words (already `u_reg_word`).

**Verification (in-container):**
- Normal `make image`: **29/29** `meson test` (goldens unchanged → behavior
  preserved).
- Diagnostic UBSan (`-Db_sanitize=undefined -shared-libsan -Wl,-rpath,…`):
  regression **23/23**, cover program clean, **0** runtime-error lines.
- Trap UBSan: regression **23/23**, cover clean, all 6 example programs clean.

**Scope note:** swept `src/` only. The `-nostdlib` example demos are out of scope
(and must not be sanitized — global `-Dc_args` instruments them and breaks their
freestanding C; build only the `spimulator` target under UBSan).

**Decision on a permanent UBSan lane (acceptance item):** recommended but **left
to Bill** — it would mean gating the image build on a UBSan `meson test` run
(like the existing suite gate), a shipped-build/CI policy choice. Not added here.
Trap mode is the reliable local gate; diagnostic alone is not (it under-reports).

## Why

Two UB sites were just fixed individually (`scanner.c` hex-shift,
`symbol-table.c` hash) — see
`tasks/archive/2026/06/16/scanner-hex-shift-overflow.md` and
`.../sym-tbl-hash-overflow.md`. While verifying those, a build of the **whole**
binary with `-fsanitize=undefined -fsanitize-trap=undefined` failed **10 of 29**
`meson test` cases (the normal build is 29/29 green). Each trap-mode failure is a
distinct UB site that *some* test exercises — so there is more undefined
behavior lurking beyond the two already fixed.

UB at `-O0` is usually benign (clang/gcc are conservative), but at `-O2` the
optimizer is licensed to assume UB "can't happen" and may elide checks or
miscompile surrounding code. spim is built `--buildtype=debug` today, but this is
a correctness landmine and worth closing.

## How to reproduce the signal

Build spim with UBSan and run the suite. Two modes:

- **Trap mode (links with no sanitizer runtime — works in this image):**
  ```sh
  CC=clang meson setup /tmp/ub /spimulator --buildtype=debug -Dline_editing=disabled \
      -Dc_args='-fsanitize=undefined -fsanitize-trap=undefined' \
      -Dc_link_args='-fsanitize=undefined -fsanitize-trap=undefined'
  meson compile -C /tmp/ub
  meson test -C /tmp/ub            # ~10/29 fail today; each = a UB site
  ```
  Trap mode aborts with SIGILL at the UB and gives no diagnostic — good for a
  pass/fail gate, bad for locating the site.

- **Diagnostic mode (needs the runtime to link):** prefer
  `-Db_sanitize=undefined` so UBSan prints `file:line: runtime error: …`. The
  standalone runtime failed to link in this image (undefined `_Unwind_*`,
  `dlsym`, `__vsnprintf_chk` — the link line doesn't pull `libgcc_s`/`libdl`).
  First task step is to make it link: add `-Dc_link_args=-Wl,--no-as-needed
  -lgcc_s -ldl` (or `-static-libsan` / `clang_rt` flags), or temporarily add the
  needed `-l`s. Then `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 meson test`
  yields exact `file:line` for every site.

## Plan

1. **Get diagnostic UBSan linking** (above) so failures report `file:line`,
   not just SIGILL.
2. **Enumerate** — run `meson test` under diagnostic UBSan; collect the unique
   `runtime error:` sites. Expect a mix of: signed-int overflow, left-shift of
   signed / shift-past-width, possibly misaligned loads, `int`↔`enum` issues,
   and pointer arithmetic. (The 10 trap failures are the floor — sites not hit
   by any test won't show; consider also running spim over `examples/` and the
   `pgu/` listings to widen coverage.)
3. **Triage & fix** each site, smallest-diff-first, matching the two fixes
   already done (accumulate/compute in unsigned where wrap is intended; cast at
   the boundary). Keep behavior identical for valid inputs — this is hygiene,
   not a semantics change.
4. **Re-verify** after each fix; goal is `meson test` green under diagnostic
   UBSan, then green under trap mode.
5. **Optional guardrail:** add a `-Dub_sanitize` (or a CI/`make` lane) that
   builds+tests under UBSan so regressions are caught. Decide whether to wire it
   into the image build (would make UB *fail the image*, like the current suite).

## Notes / decisions

- **Scope:** spim itself (`src/`). The freestanding `examples/` demos are
  `-nostdlib -O0` teaching code with their own conventions — sweep them only if
  cheap; don't let them balloon the task.
- **In-container only**, per the working arrangement. Trap mode needs no extra
  packages; diagnostic mode may need a temporary link-flag/dep addition
  (allowed as a tracked, temporary build-file change — remove before done).
- Don't silence with `__attribute__((no_sanitize(...)))` unless a site is
  genuinely intended UB-free-by-construction and documented; prefer real fixes.

## Acceptance

- `meson test` green under both `-Db_sanitize=undefined` (diagnostic) and
  `-fsanitize-trap=undefined` (trap).
- Each fixed site noted (file:line, what + why) in this doc before archiving.
- Decision recorded on whether a permanent UBSan build lane is added.
