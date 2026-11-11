# Fixed the C bugs and undefined behavior the ports exposed, and followed in every port

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Source:** tasks/review/2026/11/11/repo-review.md §4.1.7, §4.3.1, §4.3.2, §4.5.6 F12

## Summary

The maintainer decided (the record's question 1) that C is fixed first and the ports follow. Each item
was done in C, Python, Haskell and Rust in one commit, with a regression case that fails on the old
engine. All are registered in `meson.build`'s `regression_tests`; the ones the harness can compare
are also in `port/harness/corpus.txt`, which now has 33 cases.

| # | Item | Fix | Test |
|---|---|---|---|
| 4 | `.align N` for N < 0 or N ≥ 32 shifted a 32-bit mask out of range | parse error "Alignment out of range (0 .. 31)" | `range_checks` |
| 6 | `$fN` above 31 read past the FP register file; `atoi` overflowed on a long number | `register_name_to_number` returns -1, so the assembler says "Expected FP register" and the REPL's `print` treats it as a label (as it already did `$99`) | `range_checks` (also covers the moved `fp_reg_overflow` case) |
| 3 | `ld`/`sd`/`mfc1.d`/`mtc1.d` starting at register 31 wrote register 32 | parse error "Register pair starts at the last register (31)"; pairs from 30 still assemble. Odd registers were left allowed, as GNU `as` allows them for o32 `ld`/`sd` | `register_pair` (the old engine hangs) |
| 1 | `print_all_regs hex` after `print_all_regs` ended in three stale lines | `ss_clear` closes and frees the memstream instead of rewinding it; this also covers the data/text dumps and `disasm`. The ports were already right | `print_all_regs_twice` |
| 5 | `-assemble` left a trailing `", "` when a segment ended off a word | `", "` goes before each word but the first (C, Haskell, Rust; Python's join was right) | `assemble_odd_end` |
| 2 | `-sktext` below 388 bytes or `-stext` below 4 looped forever | fatal startup error naming the flag (decision 1, option (a)). `-stext 0` had the same loop, so it was included | `small_text_segments`, corpus `small_ktext`/`small_text` |

The `.align` and pair errors are reported on the instruction's own line. A new
`parse_error_on_line` (C) and its port counterparts take the line number captured before the
operands, since the last operand can consume the newline. The `make_memory` guard pair is accepted
by `tools/c_dispatch_check.py` with a `dispatch-check: ok` comment, because it checks two flags,
not one value.

Gates: the gcc and clang local builds; `meson test` 330/330 (regression, examples and the three
port suites, lints included); clang regression 47/47. The image gate runs at the end of the batch
branch.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. A text segment too small for the exception handler (`-skt 0`) is a startup error (option (a)).
