# Haskell port cleanups from the 2026-11-11 review

**Status:** proposed — needs go-ahead
**Priority:** 8
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §4.4.3, §4.4.4, §4.4.5, §4.4.6 F20

## BLUF

Code-quality items in `port/haskell/` against the Haskell standard (the haskell-coding-standard.md
reference doc from github.com/billsix/runClaudeInContainer). None changes behavior. Done means each
is fixed and `port-haskell` (with `haskell-annotations`) is green.

## Context

- **`tools/BindingTypes.hs:172-186`** (§4.4.3) reports list-comprehension generators as do-binds (a
  comprehension is an `HsDo` node too), which the standard exempts. That is why
  `OpcodeTables.hs:70-87` annotates `r :: OpRow <-`. Skip `HsDo` nodes in a `ListComp` context.
  Also: an empty `hie` directory passes with 0 findings; guard on the module count.
- **Polymorphic local signatures used at one type** (§4.4.4): `Run.hs:259,261` (Int32), `:290` (Int),
  `:620` (Word64); `Memory.hs:82` (Int), `:376`, `:533` (Word32).
- **Partial functions** (§4.4.5): `argv !! i` (`Main.hs:163,175,230`, also O(n²)), `regNames !! n`
  (`DisplayUtils.hs:48-67`), the hex-digit `!!` (`Memory.hs:535`), `last`/`init` (`CFormat.hs:64`).
  `DisplayUtils.hs:147-150` `partialLine` doesn't terminate for an unaligned address (LEAD; its
  callers look aligned).
- **Hot path and duplication** (§4.4.6): `op \`elem\` singleCompares`/`doubleCompares`
  (`Run.hs:513-520`) sit mid-`case`. `expandData`/`expandKData` are near-copies, `raiseAt` is
  duplicated, and `0xfe800000` should be named `fcsrMask`.

## Plan

1. Fix each group; keep `hlint`'s count from rising.
2. Gate: `meson test --suite port-haskell`.

## Open questions

None.
