# Make the Haskell port's register writes strict, and its sbrk non-quadratic

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`
**Source:** tasks/review/2026/11/11/repo-review.md §4.4.1, §4.4.2 F4

## Outcome

- **The leak.** `set` in `port/haskell/src/State.hs` evaluates the value before writing
  (`writeIORef r $! v`). HI/LO, like all long-lived state, are written through it. Before, a
  `madd`/`msub` loop that never read HI/LO built an ever-longer chain of unevaluated updates.
  This is the Haskell standard's "strict where state accumulates" rule. (`modify` was already
  `modifyIORef'`.)
- **Quadratic sbrk.** `expandData` (`port/haskell/src/Memory.hs`) grows the data segment's array to
  at least double its capacity, never past the limit, and copies only when the capacity runs out.
  Before, it copied the whole segment one element at a time on every call.
- **Segment ends.** Because the array can now be longer than the segment, `byteSegment` and
  `memReference` return a `SegmentRef`: the array, the offset, and the segment's logical last offset.
  `readCString` and the read_string, read and write syscalls (`Syscall.hs`) bound their copies by
  that offset instead of the array's bounds, so behavior at the end of a segment is unchanged. Bytes
  past `dataTop` are unreachable, so they stay zero.

## Verification

| Program | C | Haskell before | Haskell after |
|---|---|---|---|
| 3,000,000 × `madd $t1, $t2` | 4 MB peak | 488 MB peak (sampled) | 12 MB peak, 0.54 s |
| 20,000 × `sbrk(16)` | 0.04–0.06 s | 8.9 s | 0.06 s |

Peak RSS was read from `/proc/<pid>/status` `VmHWM`. The `port-haskell` suite passes, 74/74,
including `haskell-annotations`. `hlint`'s count for the three files is unchanged (2).
