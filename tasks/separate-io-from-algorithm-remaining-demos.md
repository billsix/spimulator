# Separate I/O from the algorithm (+ unit tests) for the remaining demos

**Status:** in progress (away-batch 2026-10-30) — maintainer authorized completing it with
discretion, gpg off on branch `separate-io-remaining-demos`, no questions. Decisions logged below.
**Priority:** 6
**Difficulty:** 4

## BLUF

The I/O-separation + unit-test pattern is done for the two little-language evaluators — `calc`
(`calc-eval`/`calc-eval-test`) and `rpn` (`rpn-eval`/`rpn-eval-test`): a pure core over an
in-memory buffer plus a no-stdin, golden-pinned unit-test demo. This task carries that same
split to the *other* I/O-coupled demos, but only where it pays off — a demo whose core is a
clean pure function worth testing independently. Deliverable per chosen demo: a `<demo>-core`
(or similarly named) variant that operates on a buffer/array, and a `<demo>-core-test` battery.
**Needs a go-ahead and the answers below before any code.**

## Context

Follow-on to `tasks/archive/2026/10/30/examples-separate-io-from-algorithm.md` (calc) and
`tasks/archive/2026/10/30/rpn-separate-io-and-unit-tests.md` (rpn), at the maintainer's request
(William Emerison Six <billsix@gmail.com>, 2026-10-30: "make a follow up task for the split").
The method is durable in `tasks/reference/incremental-example-decomposition.md` — read its
"Lessons from the first two ladders" section first, especially the I/O-separation bullet (the
pure core returns a `status` instead of printing; in asm, state kept on `$sp` in the monolith
may have to move to `.data` once the core becomes a called function).

The discriminator (from the calc/rpn work): separation + unit tests earn their keep when the
algorithm is a **nontrivial pure function hiding inside an I/O loop**. Where the "algorithm" IS
the I/O loop (a per-byte map), the split only renames the loop and adds no testable surface —
leave those whole.

## Candidate triage (to confirm — this is a proposal, not a decision)

Discovery: demos under `examples/src/` that read stdin / a file inside the algorithm
(`rg -l 'read_char|read\(|open' examples/src` at implementation time will pin the live list).
Preliminary read of the curriculum:

- **Strong candidates — a clean pure core worth a unit-test battery:**
  - `cksum` — the POSIX CRC32 is naturally `uint32_t crc32(const unsigned char* buf, int len)`:
    pure, table-driven, and highly testable against known vectors (empty, `"a"`, the system
    `cksum` golden). Highest value.
  - `bubble-sort` — a pure `sort(int* a, int n)` over an array is the textbook unit-test target
    (already sorted, reverse, duplicates, single element). The stdin read is already a separate
    rung (`bubble-sort-1-fixed` operates on a fixed array — most of the separation exists).
  - `wc` — counts (lines/words/bytes) over a buffer; a pure counter struct is testable.
- **Weak / leave whole — the loop is the lesson (per-byte or per-line map, no hidden core):**
  `tr`, `rot13`, `yes`, `clear`, `touch`, `head` (its core is "stop after N lines" — a thin
  predicate; argv is already its own rung). `strings` is borderline (printable-run extraction
  has a small state machine) — decide in Q2.

## Decisions (2026-10-30, away-batch; William Emerison Six <billsix@gmail.com> authorized, I chose)

Resolving the open questions below with discretion (maintainer away, "cksum and bubble-sort, and
anything else"):

1. **Scope = `cksum` + `bubble-sort`.** Both have a clean pure seam and existing goldens/rungs to
   reuse. **`wc` left whole / deferred** (not "anything else" material): its "algorithm" is a
   two-counter byte loop — no nontrivial pure core, the same discriminator that keeps the per-byte
   maps whole — and it has **no golden baseline today**, so decomposing it well first means giving
   `wc` its own golden + run-demo wiring, a separate coverage-gap task, not I/O separation.
2. **`strings` = out (left whole).** Borderline (a small printable-run state machine) but closer to
   "the loop is the lesson" than cksum/bubble-sort; per my own recommendation.
3. **Naming = `-core` / `-core-test`.** Uniform suffix; the evaluators keep their verb-named
   `-eval` / `-eval-test`.

Per-demo shape (one more refinement found while reading the sources):
- **`cksum`** gets BOTH — `cksum-core` (I/O-separated whole program: pure
  `unsigned int cksum_of(const unsigned char*, unsigned int)`, `my_main` slurps stdin, shares
  `cksum.expected`) and `cksum-core-test` (battery vs. system-`cksum` vectors). The existing
  `cksum-1-stdin` computes inline (not a pure function), so the factoring is genuinely new.
- **`bubble-sort`** gets ONLY `bubble-sort-core-test`. Its `bubble_sort(int*, int)` is **already** a
  pure function called from `my_main`, and `bubble-sort-1-fixed` already runs it over a fixed array —
  so a whole-program `-core` variant would just duplicate the finished demo. The missing piece is the
  unit-test battery.

## Open questions

> Resolved above (away-batch). Left here for the record.


1. Scope: do all three strong candidates (`cksum`, `bubble-sort`, `wc`), or just `cksum` +
   `bubble-sort` (the cleanest cores), or `cksum` only as a second proof? Recommendation:
   `cksum` + `bubble-sort` — both have an obviously pure core and existing goldens/rungs to
   reuse; `wc` only if you want the third.
2. Is `strings` in or out? Its printable-run extractor is a small state machine — testable, but
   closer to the "loop is the lesson" line than cksum/bubble-sort. Recommendation: out (leave
   whole) unless you want it.
3. Naming: the evaluators used `-eval` / `-eval-test` (the core's verb). For these, what suffix —
   `-core` / `-core-test`? `cksum-crc` / `cksum-crc-test`? `bubble-sort-core`? Recommendation:
   `<demo>-core` / `<demo>-core-test` as a uniform convention, documented in the reference doc.

## Done =

Each chosen demo gets a paired C+asm `-core` variant (pure function over a buffer/array, errors
reported via return/`status`, no I/O in the core) and a paired `-core-test` battery (no stdin,
`<input> => <result>` golden-pinned), wired into `examples/src/meson.build`, the
`examples/src/Makefile` inventory **if** a new lib/helper is introduced (the two build systems are
independent — see `CLAUDE.md` › `examples/`), `examples/tests/run-demo.sh`, and
`examples/READING-ORDER.md`; both sides byte-diff against goldens and the containerized
`make image` gate is green.
