# Ports of the engine (tasks/engine-ports.md).  The C engine is the standard;
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
#
# A port is listed here when its task's harness milestone is done, so the image
# build (which runs `meson test`) gates it from then on.  Each entry is
# [name, executable relative to the source root].
ports = [
  ['python', 'port/python/spimulator-python'],
]

# Not run against ports, with the reason:
#   explain   the teaching mode is not ported yet (tasks/engine-ports.md)
port_skipped_tests = ['explain']

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
endforeach
