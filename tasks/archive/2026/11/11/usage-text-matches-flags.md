# Made the usage text list every flag, in all four engines

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Found by:** `tasks/archive/2026/11/11/man-page-rewrite.md`

## Summary

The usage text printed for a bad argument (`src/spim.c`) said `Usage: spim` and left out the
segment sizes and limits, `-pseudo`/`-nopseudo`, the `-trap` aliases and every short form. It now
reads `Usage: spimulator [option ...] [-file] file [-file file ...] [program arguments]` and lists
each option the parser accepts, one line each, with its short form. The list comes from
`Documentation/spimulator.1`, whose "-explain" lines the explain-mode fix
(`tasks/archive/2026/11/11/explain-mode-correctness-bugs.md`) had just updated. Python, Haskell and
Rust each print the C string byte for byte, generated from the C literal; a bad argument gives
identical stderr and exit status in all four engines. Gates: the regression suite (51/51) and every
port suite (`meson test` 340/340).
