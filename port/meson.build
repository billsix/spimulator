# Ports of the engine (tasks/reference/engine-ports-design.md).  The C engine is the standard;
# every port in `ports` gets the C engine's own test suites, run against the
# port's executable, plus a direct comparison with the C engine:
#
#   port-<name>  suite, containing
#     <name>-<test>         every regression test (tests/run-test.sh) except
#                           the teaching mode, which the ports do not have yet
#     <name>-demo-<demo>    every examples demo (examples/tests/run-demo.sh)
#     <name>-documented     every example's documented BUILD AND RUN commands,
#                           its spimulator-<name> lines run through the port
#                           (tools/check_example_commands.py --port)
#     <name>-compare        port/harness/compare_engines.py: stdout, stderr
#                           and exit status against the C engine, case by case
#     <name>-all-programs   port/harness/compare_all_programs.py: every .s/.asm
#                           in the repo, assembled six ways and run, against C
#     <name>-repl           port/harness/compare_repl.py: each REPL session in
#                           port/harness/repl_corpus/, whole transcript, against C
#
# plus, per port, a check of its float formatter against glibc's printf (each
# port must print the C engine's five float formats byte for byte; the
# harness only tolerates a few ULP):
#   python-float-format   tools/check_float_format.py
#   haskell-float-format  port/haskell/tests/check-format.sh, and
#   haskell-strtod        port/haskell/tests/check-strtod.sh (its atof)
#   rust-cargo-test       cargo test: the cformat doctests and
#                         port/rust/tests/cformat_vs_c.rs
# and, for the Haskell port only, haskell-annotations: every do-bind and
# let/where binding has a written type (port/haskell/tests/check-annotations.sh).
# harness-doctests checks the harness's own float comparison.
#
# A port is listed here when its task's harness milestone is done, so the image
# build (which runs `meson test`) gates it from then on.  Each entry is
# [name, executable relative to the source root].
ports = [
  ['python', 'port/python/spimulator-python'],
  ['haskell', 'port/haskell/spimulator-haskell'],
  ['rust', 'port/rust/spimulator-rust'],
]

# Not run against ports, with the reason:
#   explain, explain_parity, explain_branch_targets, explain_pseudo
#             the teaching mode is not ported yet (tasks/reference/engine-ports-design.md)
port_skipped_tests = ['explain', 'explain_parity', 'explain_branch_targets', 'explain_pseudo']

foreach port : ports
  port_name = port[0]
  port_exe = meson.project_source_root() / port[1]
  port_suite = 'port-' + port_name

  foreach name : regression_tests
    if not port_skipped_tests.contains(name)
      test(port_name + '-' + name, test_driver,
           args: [port_exe, exception_file, name],
           suite: port_suite, timeout: 600)
    endif
  endforeach

  foreach name : lib_demo_tests
    test(port_name + '-demo-' + name, demo_test_runner,
         args: [examples_build_dir, port_exe, exception_file, name],
         suite: port_suite, timeout: 600)
  endforeach

  test(port_name + '-documented', python3,
       args: [meson.project_source_root() / 'tools' / 'check_example_commands.py',
              '--spimulator', spimulator_exe.full_path(),
              '--port', port_name + '=' + port_exe],
       depends: spimulator_exe,
       suite: port_suite, timeout: 1800)

  test(port_name + '-compare', python3,
       args: [meson.current_source_dir() / 'harness' / 'compare_engines.py',
              '--reference', spimulator_exe.full_path(),
              '--engine', port_exe],
       depends: spimulator_exe,
       suite: port_suite, timeout: 3600)
  test(port_name + '-all-programs', python3,
       args: [meson.current_source_dir() / 'harness' / 'compare_all_programs.py',
              '--reference', spimulator_exe.full_path(),
              '--engine', port_exe],
       depends: spimulator_exe,
       suite: port_suite, timeout: 3600)
  test(port_name + '-repl', python3,
       args: [meson.current_source_dir() / 'harness' / 'compare_repl.py',
              '--reference', spimulator_exe.full_path(),
              '--engine', port_exe],
       depends: spimulator_exe,
       suite: port_suite, timeout: 1800)
endforeach

test('haskell-annotations',
     find_program('bash'),
     args: [meson.current_source_dir() / 'haskell' / 'tests' / 'check-annotations.sh'],
     suite: 'port-haskell', timeout: 1800)

test('python-float-format', python3,
     args: [meson.project_source_root() / 'tools' / 'check_float_format.py'],
     suite: 'port-python', timeout: 600)
test('haskell-float-format', find_program('bash'),
     args: [meson.current_source_dir() / 'haskell' / 'tests' / 'check-format.sh'],
     suite: 'port-haskell', timeout: 1800)
test('haskell-strtod', find_program('bash'),
     args: [meson.current_source_dir() / 'haskell' / 'tests' / 'check-strtod.sh'],
     suite: 'port-haskell', timeout: 1800)
cargo = find_program('cargo', required: false)
if cargo.found()
  test('rust-cargo-test', cargo,
       args: ['test', '--offline', '--quiet', '--manifest-path',
              meson.current_source_dir() / 'rust' / 'Cargo.toml'],
       suite: 'port-rust', timeout: 1800)
endif
test('harness-doctests', python3,
     args: ['-m', 'doctest', meson.current_source_dir() / 'harness' / 'compare_engines.py'],
     suite: 'regression')

# Each port's tier-(a) lint gate (its coding standard's tools), one script per port:
#   port-python-lint   ruff, ty, the annotate-every-binding check, the doctests
#   port-rust-lint     rustfmt and clippy (warnings are errors)
#   port-haskell-hlint hlint, advisory: always passes; its hints are in the test log
test('port-python-lint', find_program('bash'),
     args: [meson.current_source_dir() / 'python' / 'lint.sh'],
     suite: 'port-python', timeout: 600)
test('port-rust-lint', find_program('bash'),
     args: [meson.current_source_dir() / 'rust' / 'lint.sh'],
     suite: 'port-rust', timeout: 1800)
test('port-haskell-hlint', find_program('bash'),
     args: [meson.current_source_dir() / 'haskell' / 'lint.sh'],
     suite: 'port-haskell', timeout: 600)
