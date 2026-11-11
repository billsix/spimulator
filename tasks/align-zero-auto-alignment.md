# Decide what `.align 0` means in text: disable auto-alignment, or not

**Status:** proposed — needs go-ahead
**Priority:** 7
**Difficulty:** 2
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §10.5 F16

## BLUF

`enable_text_auto_alignment` (`src/instruction.c:71`) is written by `.align` and never read, so
`.align 0` in text doesn't disable auto-alignment, which upstream's `db67bc9` apparently intended.
The question was left open in an archived task and is tracked nowhere. Done means a decision, and
either the variable read where text items are placed (in every engine), or the variable and its
writers removed.

## Context

- `src/instruction.c:66-72` carries `[[maybe_unused]] static bool enable_text_auto_alignment`.
- The 2026-11-10 review's §3.25 found it write-only, and
  `tasks/archive/2026/11/05/upgrade-to-fedora-45.md:31-35` left the question open.
- Upstream SPIM's `.align 0` semantics (git history: `git log -S enable_text_auto_alignment`).
- Any behavior change goes to every port (`CLAUDE.md`).

## Plan

1. Read upstream's intent from `db67bc9` and the SPIM manual.
2. Decide (question 1), then either honor the flag in C and every port with a regression test, or
   delete it and its writers.
3. Gate: `make image`.

## Open questions

1. Should `.align 0` in text disable auto-alignment (a), or should the dead flag go (b)? Recommend
   (a) if upstream's manual documents it, else (b).
