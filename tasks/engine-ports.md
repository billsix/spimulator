# Implemented the spimulator engine three more times, with the C engine as the standard

**Created:** 2026-11-10 · **Completed:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)

## Summary

The maintainer asked for several implementations of the engine side by side, with the C engine as the
standard and every other one required to behave the same. On one day, on branch `engine-ports`, the
agent:
- fixed the C engine's known bugs first (`tasks/archive/2026/11/10/engine-port-prereq-c-fixes.md`);
- built a shared comparison harness (`port/harness/`, `port/meson.build`);
- ported the engine to Python, GHC Haskell and Rust
  (`tasks/archive/2026/11/10/engine-port-{python,haskell,rust}.md`).

Each port matches the C engine on the C engine's own suites and on every assembly program in the
repo, with no float-formatting warnings. Writing the ports turned up nine more C engine bugs, each
fixed in C with a test (`a0eefb8`, `6c27911`). The rules every port follows are in
`tasks/reference/engine-ports-design.md`. The teaching mode is not ported yet:
`tasks/engine-ports-teaching-mode.md`, blocked on the C redesign.

## Log (2026-11-10, the autonomous day run)

- The prerequisites were done first and archived (`tasks/archive/2026/11/10/engine-port-prereq-c-fixes.md`).
- **The shared harness was built by the coordinator, ahead of the Python port** (`port/harness/`,
  `port/meson.build`, `entrypoint/05-install-ports.sh`; commit `0e618c7`), so all three ports are
  judged by the same tests from the start. The Python task's Plan step 2 is therefore already done.
- **The Haskell port started in parallel with the Python port, in a separate git worktree** (its
  own branch, merged into `engine-ports` when done). The maintainer's order (Python, then Haskell,
  then Rust) is kept for finishing and merging: the files every port touches (the `ports` list in
  `port/meson.build`, the example headers, `tools/check_example_commands.py`) are changed only
  after the Python port's version is in. Options weighed: strictly one port after another (simple,
  but the Haskell and Rust ports would start late in the day and finish less), or all three at
  once (more parallel, but three ports building at the same time compete for the machine, and three
  sets of shared-file edits to reconcile). The agent's call, under "use your discretion".
- **The Rust port also started right away, in its own worktree**, once the machine's headroom was
  checked (16 cores, 30 GB of memory). Same rule: it is merged and registered after the Haskell
  port.
- Each port is written by a separate agent with a written brief. The coordinator reviews its
  commits, merges, registers the port, runs the gate (`make image`), and writes the done-record.
- **The Python port finished first** (commits `e054044`..`4946387`; registered in `port/meson.build`).
  The gate `make image` with it registered passed: 128 meson tests (64 of them the `port-python`
  suite), and the regression suite under UBSan and ASan.
- **The Haskell port finished second** and was merged (`eff193a`), but not registered yet (below).
- **The ports found nine more C engine bugs between them** (logged in each port's task, "C engine
  bugs found"). Six were fixed in C (`a0eefb8`, each with a test that fails before the fix), because
  the C engine is the standard and two ports had already chosen different behavior where C crashed.
  Every port is then brought in line with the fixed C. Options weighed: (a) leave C alone and make
  the ports copy its crashes (what the Python port did: exit 139 to match a SIGSEGV), or (b) fix C
  first. (b), because a port should not be written to reproduce a null-pointer crash, and the
  fixes were small.
- **Two pieces of the Python port's work were promoted for every port:** its whole-tree comparison
  (`port/harness/compare_all_programs.py`, registered as `<name>-all-programs`) and its example-header
  codemod (`tools/add_port_lines.py NAME LABEL`).
- **The Rust port finished third**, merged (`0427cf2`) and registered (`b965190`). It found the
  same six C bugs independently, and three more: crashes on `.word -2147483648 / -1` and on a
  value-taking flag given last, and a silently ignored `-exception_file` given last. All three were
  fixed in C (`6c27911`), and all three ports followed.
- **The whole-tree comparison now skips four cases by name** (`tests/timer.s`, `tests/tt.io.s` in the
  run and `-listing` modes). Without `-mapped_io` they run to the 2³¹−1 step limit, which the debug
  C build reaches in about 14 minutes, so the comparison tested nothing.
- **A rare hang that looked like a flaky port test was the C engine on `tests/tt.core.s`.** Twice a
  port suite failed because the C reference ran forever under parallel load. It was caught with gdb:
  upstream's timer test waited for CP0 Count to equal exactly 10, and Count, which follows real time,
  can step from 9 to 11 when the engine is descheduled. Fixed in the test (`904d7d9`, wait while
  Count < 10); no hang in 384 loaded runs.
- **Left for later:** the teaching mode (`tasks/engine-ports-teaching-mode.md`, blocked on the C
  redesign), and upstream's `tt.alu.bare.s` (`tasks/alu-bare-test-assembler-strictness.md`, needs a
  decision).
