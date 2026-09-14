# Trim spimulator's CLAUDE.md (9,311 B ≈ 2.3K tok, loaded every session)

**Status:** Done — trimmed 2026-09-13 (pending archive after the work commit)
**Priority:** 5
**Difficulty:** 3

**Result:** CLAUDE.md 9,311 B → 5,385 B (−3,926 B, ~42%). Deleted the rotting
in-flight/archived Tasks snapshot (replaced with a pointer to `tasks/` +
`tasks/README.md`); moved the parser flex+bison/PARSE_DIRECT history to
`tasks/reference/parser-ast-emit.md` (the flex+bison Phase 5 fact was appended
there, as it was not yet captured); shrank the Tests sanitizer rationale to a
one-line pointer to `tasks/archive/2026/06/16/ubsan-sweep.md`. No new reference
doc needed.

## BLUF
spimulator's `CLAUDE.md` is 9,311 B — the largest of the three C/toolchain repos —
and it is spliced into the AI's context on every session/turn. Roughly a third of it
is a hand-maintained snapshot of the open/archived task set that duplicates `tasks/`
and `tasks/README.md` and rots constantly; another chunk is build/parser/sanitizer
*rationale* that already lives in archive and reference docs. Delete the task
snapshot, trim the history down to one-line pointers, and the file should drop to
roughly 5–5.5 KB with no operational loss.

## Context
- Why: `CLAUDE.md` loads every session; detail belongs in `tasks/reference/`. Method
  and per-turn token numbers: runCrushInContainer
  `tasks/reference/crush-context-assembly.md`.
- Current size: 9,311 B ≈ 2.3K tok. `@`-imports: **none** (no bare `@path` lines).
- Convention: `CLAUDE.md` lean + operational; durable detail in `tasks/reference/`.
- Existing `tasks/reference/` docs (destinations already exist for most rationale):
  - `parser-ast-emit.md` — parser/AST/emit design (covers the Status parser history).
  - `mips-fpu-and-float-demos.md` — FPU + AST-via-sbrk notes.
  - `inherited-idiosyncrasies.md` — inherited-code oddities.
  - Archive doc `tasks/archive/2026/06/16/ubsan-sweep.md` — sanitizer/UB rationale
    (already cited in CLAUDE.md; the deep sanitizer prose can shrink to this pointer).

## Stay vs move (section-by-section)
| section (heading) | ~bytes | Verdict | Destination |
|---|---|---|---|
| Title + "A fork of SPIM…" intro (L1–7) | ~430 | STAY | CLAUDE.md |
| `## Status` (L9–21) | ~620 | TRIM | keep 2–3 current-state bullets; drop the "flex+bison removed Phase 5" / "PARSE_DIRECT removed 2026-08-03" history — already in `tasks/reference/parser-ast-emit.md` |
| `## Layout` (L23–43) | ~1,300 | STAY | CLAUDE.md (brief, operational) |
| `## Build / container workflow` (L45–60) | ~950 | STAY | CLAUDE.md |
| `## Tests` (L62–77) | ~1,050 | TRIM | keep the gate + `RUN_SANITIZERS=0` opt-out; move the ASan-default-options / "UBSan diagnostic under-reports, trap is the reliable gate" rationale to a one-line pointer to `tasks/archive/2026/06/16/ubsan-sweep.md` (already cited) |
| `## Conventions` (L79–89) | ~750 | STAY | CLAUDE.md (invariant: opcodes.h single-source, `BUILD_TREE_SITTER=1` gate rule, teaching-output-is-first-class) |
| `## Tasks (in-flight)` (L91–167) | ~3,000 | MOVE/DELETE | delete the whole snapshot + the archived-task listings; replace with a 2-line pointer to `tasks/` and `tasks/README.md` (§Ordering & dependencies), which are the live record. This is the single biggest win. |

## Projected result
- Removing the ~3.0 KB Tasks snapshot and trimming ~0.6 KB of Status/Tests history
  brings the file to roughly **5.3 KB ≈ 1.3K tok** — a ~45% cut with zero loss of
  operational guidance (every deleted fact already lives in `tasks/`,
  `tasks/README.md`, or an existing reference/archive doc).
- No new reference doc is required: the destinations already exist. The Tasks
  snapshot is pure duplication of `tasks/` and needs deletion, not relocation.

## Open questions
1. Keep a *one-line* pointer to the two big-swing task docs (`timing-model.md`,
   `software-alu.md`) in the trimmed Status, or leave discovery entirely to `tasks/`?
   Recommendation: leave it to `tasks/` — that is what the directory is for.

## Related
- runCrushInContainer `tasks/reference/crush-context-assembly.md` — measurement + method.
- `tasks/reference/parser-ast-emit.md`, `tasks/archive/2026/06/16/ubsan-sweep.md` —
  where the trimmed rationale already lives.
