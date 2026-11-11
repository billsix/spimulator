# Ports of the spimulator engine

Implementations of the engine in other languages, side by side with the C one. **The C engine is the
standard**: each port must behave the same (same stdout, stderr and exit status), checked against
it. How a floating-point number is printed is the one difference allowed; it is reported as a
warning. Rules, ordering and decisions: `tasks/reference/engine-ports-design.md`.

| Port | Directory | Command | Task |
|---|---|---|---|
| Python | `port/python/` | `spimulator-python` | `tasks/archive/2026/11/10/engine-port-python.md` |
| GHC Haskell | `port/haskell/` | `spimulator-haskell` | `tasks/archive/2026/11/10/engine-port-haskell.md` |
| Rust | `port/rust/` | `spimulator-rust` | `tasks/archive/2026/11/10/engine-port-rust.md` |

Each port takes the C engine's flags and arguments. The teaching mode (`-explain`) is not ported
yet.

Each port's opcode table is generated from `include/opcodes.tsv` by `tools/gen_opcodes.py`
(`make generate`), like the C engine's. A port in a new language adds an emitter there:
`tasks/reference/opcode-table-generator.md`, "Adding an engine port in a new language".

## Testing a port

    # [CONTAINER] the C engine's own suites, run against a port, plus the comparison:
    meson test -C builddir --suite port-python

    # [CONTAINER] just the case-by-case comparison with the C engine:
    port/harness/compare_engines.py --reference builddir/spimulator --engine port/python/spimulator-python

`port/meson.build` registers, for each port in its `ports` list:
- every regression test except the teaching mode, every examples demo, and the documented example
  commands;
- `port/harness/compare_engines.py` (its cases are in `port/harness/corpus.txt`),
  `port/harness/compare_all_programs.py` (every `.s`/`.asm` in the repo) and
  `port/harness/compare_repl.py` (the REPL sessions in `port/harness/repl_corpus/`, whole
  transcripts, covering every REPL command);
- a check of the port's float formatter against glibc (`python-float-format`,
  `haskell-float-format` and `haskell-strtod`, `rust-cargo-test`);
- the port's lint gate, `port/<port>/lint.sh` (`port-python-lint`, `port-rust-lint`, and
  `port-haskell-hlint`, which is advisory);
- for Haskell, `haskell-annotations`.
