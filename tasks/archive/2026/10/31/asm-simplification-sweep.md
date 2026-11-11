# Simplified the MIPS assembly demos for novices (umbrella)

**Completed:** 2026-10-31
**Durable style:** `tasks/reference/mips-asm-teaching-style.md`

Every non-exempt hand-written MIPS `.asm` teaching demo was rewritten so a novice sees C values
living on the stack rather than scattered across registers. Each C local got a canonical home on
the stack with scratch registers used only as working copies, and each routine gained a columnar
SYMBOL TABLE plus an ascii-art stack-frame snapshot with traced values. The work was a pilot
(step 1) followed by nine per-topic rollout steps, on the maintainer's go-aheads "roll out the
asm sweep topic by topic" and "continue with lang, then algorithms and transforms" (William
Emerison Six <billsix@gmail.com>). Every converted demo was byte-identical to its pre-change
output, 47/47 annotated demos passed `tools/check_asm_canonical.py --all`, and all golden-tested
demos stayed green.

## The request

From William Emerison Six <billsix@gmail.com>, 2026-10-31: make the assembly simpler for a novice,
with "as few registers used as possible, I know it is less performant," so the stack frames show
"all over the place, so that the students can see the relationship of the c to the assembly, and can
forget as much as possible about what C values are stored in which registers, because they'll mainly
see the values on the stack."

## Design decisions (2026-10-31)

- **Shape B over Shape A.** Every C local gets a stack home and registers hold only working copies
  loaded to operate and stored back. Shape A (keep register-only demos and show stack art only where
  a frame already existed) was rejected because the maintainer wanted the stack view everywhere.
- **This reversed the 2026-05-17 "register-only where locals fit in temporaries" decision**
  corpus-wide, knowingly. That decision is recorded in
  `tasks/archive/2026/05/23/PLAN-asm-comments.md` ("Most of those frames were ceremony"); the style
  doc states the new default and why.
- **Columnar SYMBOL TABLE, one per routine**, extending that doc's L2 layer, with six columns in
  this order: `C variable | memory addr | register | C type | bytes | role / notes`. A stack-homed
  value fills both `memory addr` (its `-N($fp)` home) and `register` (its current scratch copy); a
  register-only value (`argc` at entry) has `—` in `memory addr`; a `.data` value has `—` in
  `register`. Table and diagram comment blocks are exempt from the 80-column rule; instruction lines
  stay within 80. The earlier disciplines were kept: ABI register names (`$fp`, not `$s8`) and
  preserved intentional bugs.
- **Traced values.** Each frame snapshot shows concrete runtime values from one canonical input per
  demo (e.g. `factorial 5` → `result = 60` at loop pass 4), slot value and register shadow both
  shown, at only a few key points per demo to limit rot. Static layout alone was rejected. Because
  stdout goldens cannot catch a stale value in a comment, a checker that re-derives snapshot values
  from a real spim run was required; it became `tools/check_asm_canonical.py`.
- **Deliberate-lesson files were exempt** (the maintainer's call), named in the style doc as
  intentional counter-examples:
  - `examples/src/extras/subrountines/subrountines-1.asm` vs `-2.asm`: the longhand-stack vs
    register calling convention contrast is the demo.
  - `examples/src/recursion/get-char-from-user/get-char-from-user-1.asm` vs `-2.asm`: a deliberate
    bug/fix pair.
  - `examples/src/intro/exit/exit.asm`: the "set `$v0` before `jr $ra`" exit lesson.
  - The `$sp` operand-stack rpn files. This set started as `rpn.asm` and `rpn-1-stack.asm` and was
    expanded to four in step 9 (adding `rpn-2-ops.asm` and `rpn-3-float.asm`), flagged for the
    maintainer to confirm.

The safety net was `examples/tests/run-demo.sh`, which runs both the C binary and the asm under
spim and diffs stdout against pinned goldens; since a register-to-stack move does not change
stdout, a green golden showed a faithful rewrite. Demos without goldens were gated by before/after
output equivalence.

## Steps

| Step | Topic | Converted | Exempt / not converted | Record |
|---|---|---|---|---|
| 1 | pilot | `increment-ints-1`, `factorial`, `gcd`, `wc`; wrote the style doc and checker; maintainer signed off on the style and the transient-temporary boundary | | `tasks/archive/2026/10/31/asm-simplification-sweep-step-1-pilot-and-style-doc.md` |
| 2 | `examples/src/intro/` | `print1through10` (loop counter to the stack), `clear`, `helloworld`, `yes` (SYMBOL TABLE only) | `exit.asm` | `tasks/archive/2026/10/31/asm-simplification-sweep-step-2-intro.md` |
| 3 | `examples/src/arguments/` | `echo`, `factor`, `seq`, `tee` (`int fds[8]` onto the stack, `static buf` in `.data`); added `examples/src/arguments/tee/tee.input` | | `tasks/archive/2026/10/31/asm-simplification-sweep-step-3-arguments.md` |
| 4 | `examples/src/recursion/` | `fibonacci`, `hanoi`, `queens` (regularized to `$fp`); queens gated by equivalence (trailing-space output) | `get-char-from-user-1`/`-2` | `tasks/archive/2026/10/31/asm-simplification-sweep-step-4-recursion.md` |
| 5 | `examples/src/extras/` | `commaAndPeriodCounter`, `print-out-ascii-1` (equivalence-gated, 256-line output), `testStringsForEquality-1` (uninitialized-return bug preserved) | `subrountines-1`/`-2` | `tasks/archive/2026/10/31/asm-simplification-sweep-step-5-extras.md` |
| 6 | `examples/src/fileio/` | all 9 (touch, nologin, cat, cp, comm, the four cksum files); buffers and CRC table in `.data`; checker green on 7 | | `tasks/archive/2026/10/31/asm-simplification-sweep-step-6-fileio.md` |
| 7 | `examples/src/lib/` | the 7 `*-demo` drivers; golden-tested, no `#CANONICAL` (the single-file checker cannot load the library) | the 4 library files, left register-based (maintainer: drivers only) | `tasks/archive/2026/10/31/asm-simplification-sweep-step-7-lib.md` |
| 8 | `examples/src/lang/` | all 10 calc demos; parser state stays global/heap, each parse routine homes its running local at `-N($fp)` | | `tasks/archive/2026/10/31/asm-simplification-sweep-step-8-lang.md` |
| 9 | `examples/src/algorithms/` | 9 (binary-search, bubble-sort ×3, fizzbuzz, pascals-triangle, sieve, rpn-eval, rpn-eval-test) | the 4 `$sp`-stack rpn rungs | `tasks/archive/2026/10/31/asm-simplification-sweep-step-9-algorithms.md` |
| 10 | `examples/src/transforms/` | all 14 non-pilot demos (base64, cut, expand, head ×2, nl, od, rev, rot13, strings, tac, tail, tr, uniq) | | `tasks/archive/2026/10/31/asm-simplification-sweep-step-10-transforms.md` |

Each rollout step-task was written when its turn came rather than pre-stubbed.
