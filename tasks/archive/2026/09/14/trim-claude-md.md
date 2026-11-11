# Trimmed spimulator's CLAUDE.md

**Completed:** 2026-09-13 (archived 2026-09-14)

`CLAUDE.md` was 9,311 B (about 2.3K tokens), the largest of the three C/toolchain repos, and it
is loaded into the AI's context every session. About a third was a hand-maintained snapshot of
the open/archived task set that duplicated `tasks/` and `tasks/README.md` and went stale
constantly; another part was build/parser/sanitizer rationale that already lived in reference and
archive docs. It had no `@`-imports. The method and per-turn token figures came from
runCrushInContainer's `tasks/reference/crush-context-assembly.md`.

## What was done

`CLAUDE.md` went from 9,311 B to 5,385 B (−3,926 B, about 42%), close to the projected 5.3 KB, with
no loss of operational guidance.

| Section | Verdict | What happened |
|---|---|---|
| Title + intro | kept | |
| `## Status` | trimmed | Kept the current-state bullets; moved the flex+bison and PARSE_DIRECT removal history to `tasks/reference/parser-ast-emit.md`, appending the flex+bison Phase 5 fact, which that doc lacked. |
| `## Layout` | kept | |
| `## Build / container workflow` | kept | |
| `## Tests` | trimmed | Kept the sanitizer gate and the `RUN_SANITIZERS=0` opt-out; replaced the ASan-options and trap-vs-diagnostic UBSan rationale with a one-line pointer to `tasks/archive/2026/06/16/ubsan-sweep.md`. |
| `## Conventions` | kept | |
| `## Tasks (in-flight)` (~3 KB) | deleted | Replaced the snapshot and archived-task listings with a pointer to `tasks/` and `tasks/README.md` (§Ordering & dependencies), the live record. |

No new reference doc was needed; every removed fact already had a home.

The open question of keeping a one-line pointer to the two large task docs (`timing-model.md`,
`software-alu.md`) in the trimmed Status was settled by the recommendation: no pointer was added,
and discovery was left to `tasks/`.
