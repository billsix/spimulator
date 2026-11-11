# Cleaned up the Haskell port against the Haskell standard

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Source:** tasks/review/2026/11/11/repo-review.md §4.4.3, §4.4.4, §4.4.5, §4.4.6 F20

## Summary

No output changed. A fresh subagent did the work with git forbidden, and the coordinator reviewed
and committed it.

- **`port/haskell/tools/BindingTypes.hs`:** the `.hie` file records a list comprehension and a do
  block alike as `HsDo` with no context tag, so the checker tells them apart by the source text at
  the node's start (`[` vs `do`/`mdo`/`M.do`; `Located.locAbove` now keeps ancestor nodes). Generators
  are exempt; a do-bind nested inside a comprehension is still checked. An empty hie directory now
  fails ("no .hie files in DIR"). The four `r :: OpRow <-` annotations in the hand-written
  `src/OpcodeTables.hs` were dropped; nothing generated needed a change.
- **Concrete local types:** `Run.hs` (`signedProduct`/`unsignedProduct :: Int32 -> Int32 -> Word64`,
  `compareFp`'s `result`, `multiplyAdd`'s `t :: Word64`) and `Memory.hs` (`instSlots :: Int -> Int`;
  the shift `sh :: Word32 -> Int`, not Word32 as the review suggested, since it is a shift amount),
  plus a few the review did not list. `unknown`/`runErrorQuiet :: IO a` stayed polymorphic: they
  never return, and the type says so.
- **Partial functions:** `Main.hs`'s argument loop walks the list (a `loadFile` helper covers `-file X`
  and a bare name); `InstFormat` `reg` and `DisplayUtils` `formatRegisters` build from the lists
  (`zipWith3`, `transpose . chunksOf n`); `Memory` `hex` is `showHex`; `CFormat` `stripZeros` and
  `Scanner` `maybeSnapshotLine` pattern-match instead of `last`/`init`. `partialLine` prints a
  computed word count, so it terminates for any address. For an unaligned address the C engine
  loops forever, but every caller passes an aligned one, so the outputs agree everywhere they can
  be compared.
- **Hot path and duplication:** the FP compare lookups moved to the final wildcard as `Set.member`
  (`containers` added to the `.cabal`; GHC ships it). `fcsrMask` (C `FCSR_MASK`) is named. A single
  `raiseAt` serves `badMemRead`/`badMemWrite`. `expandData`/`expandKData` share `growSegment`; kernel
  data now uses the same spare-capacity growth, which changes no output, because reads stop at
  `kDataTop` and the extra bytes stay zero.

Gates: `meson test --suite port-haskell` 84/84 (`haskell-annotations`, `port-haskell-hlint` and the
comparisons with C); hlint 51 → 48 hints.
