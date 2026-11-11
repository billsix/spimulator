# Decide what `.align 0` means in text: disable auto-alignment, or not

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`
**Source:** tasks/review/2026/11/11/repo-review.md §10.5 F16

## Outcome

Option (b): the dead flag is gone. `enable_text_auto_alignment` and its three writers were removed
from `src/instruction.c`, and `align_text(0)` is an explicit no-op, with a comment saying why.
Nothing changes in what the assembler produces, and the ports never had the flag.

## The evidence

- **The manual.** `Documentation/H_P_AppendixA/HP_AppA.html` says ".align 0 turns off automatic
  alignment of .half, .word, .float, and .double directives until the next .data or .kdata
  directive". That is data-segment behavior.
- **The code.** Automatic alignment happens only in the data segments: every `set_data_alignment`
  call in `src/parser.c` is guarded by `if (data_dir)`. In text, directives are never auto-aligned,
  and instructions are always word-aligned, so there is nothing for `.align 0` to turn off.
- **History.** Upstream added the text flag in 2021 (`2d506c1`, "Fix .align") as a twin of the data
  flag, and never read it. It was marked `[[maybe_unused]]` in `db67bc9`, when LLVM 23 started
  warning about it.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. Read upstream's manual and history first; honor `.align 0` in text only if they document it.
   They don't, so the flag was removed.

## Seen, not changed (LEAD)

`k_text_begins_at_point` sets `in_kernel = false`, which looks wrong for kernel text. Upstream
`6cb6a08` has the same line; it may be harmless if the callers set `in_kernel` afterwards. Not
investigated.

## Verification

gcc and clang build warning-clean; regression passes (41/41).
