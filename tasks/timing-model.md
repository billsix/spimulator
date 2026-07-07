# Optional timing model: make performance perceptible (H&P ch. 1)

**Status:** proposed — investigate → plan → execute, in that order
**Created:** 2026-07-07

## Request (Bill, 2026-07-07)

For teaching performance: add an **optional** timing delay so that executing a
program takes perceptibly different amounts of time depending on what it does.
Cost model in increasing order: each instruction has a base cost; each
register read/write adds a little; each RAM read/write adds noticeably more.
Students should be able to *tell the difference* (e.g. between a
register-resident loop and the same loop hitting memory every iteration).
Probably active only when the code is not being debugged (plain `run`), not
while stepping. Tie the numbers and the reporting to the performance
equations at the beginning of Hennessy & Patterson (Patterson & Hennessy,
*Computer Organization and Design* ch. 1.6): CPU time = instruction count ×
CPI × clock cycle time.

## Phase 1 — investigate

- Where the dispatch loop lives (`src/run.c`, `run_program`/the big switch)
  and whether an instruction-count already exists to build on.
- Where register file and memory accesses are centralized enough to meter:
  `R[]` accesses are open-coded in `run.c` (hard to hook individually);
  memory goes through `read_mem_*` / `set_mem_*` in `src/memory.c` (easy to
  hook). Decide whether register costs are *derived from the decoded operand
  shape* (count rs/rt/rd per `op_type` — no code restructuring needed) rather
  than metered at access time. That derivation approach looks right; verify.
- How "being debugged" is detectable: stepping (`step` command), active
  breakpoints, or `-explain` narration — enumerate the states where the delay
  should be suppressed (single-stepping is already human-paced; adding delay
  there teaches nothing and annoys).
- Prior art: does upstream SPIM's `-quiet`/cycle-counting or the exceptions
  path have any cycle notion to stay consistent with? Check H&P's MIPS
  single-cycle vs multicycle chapter for defensible default costs.

## Phase 2 — plan (sketch to be validated by phase 1)

- **Cost model (cycles, configurable):** base cost per instruction class
  (e.g. ALU 1, branch 1, mult/div higher), + per register operand read/write
  (small), + per memory access (large, e.g. 10–100× a register). Defaults
  chosen so a memory-heavy loop is *visibly* slower at the terminal.
- **Two outputs:**
  1. **Accounting** (always cheap): cumulative cycle count; at program exit
     print the H&P breakdown — instruction count, cycles, effective CPI, and
     "CPU time = IC × CPI × cycle time" evaluated for a stated clock rate.
     This is the direct hook to the book's ch. 1 equations.
  2. **Perceptible delay** (the new flag, e.g. `-timing[=cycle_ns]`): after
     each instruction, sleep proportionally to its cycle cost (batched —
     e.g. accumulate and `nanosleep` every N cycles — so syscall overhead
     doesn't swamp the model). Off by default; suppressed while
     stepping/debugging per phase 1's findings.
- **Explain-mode tie-in (optional):** at L2+, a per-instruction "cost: N
  cycles (1 base + 2 reg + 1 mem×20)" line, so the model is inspectable
  instruction-by-instruction.
- CLI: flag + man-page + usage string; REPL `timing on/off` maybe later
  (same runtime-toggle blocker pattern as `explain` had).
- Tests: cycle-accounting golden for a small fixed program (deterministic);
  the *delay* path is wall-clock and stays untested beyond "flag parses and
  program still produces correct output."

## Phase 3 — execute

Only after phases 1–2 are reviewed. Implementation likely: a small
`src/timing.c` (cost tables keyed off the existing `op_type` tags +
accumulator + reporter), hooks in `run.c`'s dispatch and `syscall.c`'s exit
path, flag parsing in `spim.c`.

## Open questions

- Default cycle costs: single-cycle-datapath flavor (every instruction 1
  cycle, memory stalls added) vs multicycle flavor (lw=5, sw=4, R=4, br=3, per
  H&P's classic multicycle chapter)? The multicycle numbers are more
  H&P-quotable; decide in phase 2.
- Should syscalls (I/O) carry a cost? Probably excluded from the model —
  H&P ch. 1 is about CPU time.
- Interaction with `software-alu.md` (bit-level ALU): that task makes
  execution *actually* slower; this one *models* slowness. They're
  complementary but should agree on vocabulary (cycles).
