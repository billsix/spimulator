# examples/src — paired C + MIPS-asm teaching demos.  Invoked
# via `subdir('examples/src')` from /spimulator/meson.build.
# Standalone build is no longer supported; the merged project
# is the canonical entry point.

# Educational flags — shape readable codegen for the demo binaries.
# No frame pointer, no CFI/unwind tables, no stack canaries.
# Scoped per-target (NOT add_project_arguments) so they don't
# leak into spim's compile, which has its own warning/std flags.
#
# Note: the human-readable .s *listings* (the "compiler's
# translation" teaching artifact) are no longer generated here.
# That moved to src/Makefile, which emits a clean, cleanly-named
# `<demo>.s` beside each `<demo>.c`/`<demo>.asm` — no -g debug
# noise, no `.p/<munged>.c.s` path.  See
# tasks/PLAN-asm-listings-makefile.md (supersedes the archived
# PLAN-asm-listings.md `-save-temps` approach).
edu_args = [
  '-O0',
  '-fomit-frame-pointer',
  '-fno-asynchronous-unwind-tables',
  '-fno-unwind-tables',
  '-fno-stack-protector',
]

# Each demo is freestanding — no libc, defines its own `_start`,
# reaches the kernel through inline-asm syscalls in os.h.
# Same scoping discipline: per-target, not project-wide
# (spim itself links against libc + libedit).
edu_link_args = ['-nostdlib']

# Where `meson install` puts the demo binaries.  NOT ${prefix}/bin —
# example programs don't belong on PATH.  libexec/spimulator matches
# where an RPM would ship internal/demo binaries (%{_libexecdir}/<pkg>;
# FHS: binaries not intended to be executed directly by users).
# See tasks/examples-install-location.md.
example_install_dir = get_option('libexecdir') / 'spimulator'

# Static lib of the small IO helpers shared across every demo.
io_lib = static_library('examples_io',
  files(
    'count-chars.c',
    'integer-to-string.c',
    'string-to-int.c',
    'print-string.c',
    'print-int.c',
    'print-uint.c',
    'print-char.c',
    'read-char.c',
    'read-int.c',
  ),
  c_args: edu_args,
)

# libctype — ASCII character classification + case conversion.
# Adapted from musl libc src/ctype/.  See src/lib/libctype/LICENSE-musl.
libctype_lib = static_library('libctype',
  files('lib/libctype/libctype.c'),
  c_args: edu_args,
)
libctype_inc = include_directories('lib/libctype')

# libstr — naive string/memory primitives (strlen, strcmp, strncmp,
# strcpy, strncpy, strchr, memchr, memcpy, memset, memmove).
# Adapted from musl libc src/string/.  See src/lib/libstr/LICENSE-musl.
libstr_lib = static_library('libstr',
  files('lib/libstr/libstr.c'),
  c_args: edu_args,
)
libstr_inc = include_directories('lib/libstr')

# libstdlib — atoi (and, growing: abs, labs, bsearch, _Exit).
# Adapted from musl libc src/stdlib/ and src/exit/.
# See src/lib/libstdlib/LICENSE-musl.
libstdlib_lib = static_library('libstdlib',
  files('lib/libstdlib/libstdlib.c'),
  include_directories: libctype_inc,
  c_args: edu_args,
)
libstdlib_inc = include_directories('lib/libstdlib')

# Demos can find io.h / os.h here.
inc = include_directories('.')

# One executable per demo.  Pairs are [target_name, source_path].
# Demos are grouped by topic folder (intro, algorithms, transforms,
# fileio, arguments, recursion, extras) — see ../READING-ORDER.md for
# the engagement-first sequence across these topics.
demos = [
  # ---- intro: the smallest possible programs ----
  ['exit',                   'intro/exit/exit.c'],
  ['helloworld',             'intro/helloworld/helloworld.c'],
  ['print1through10',        'intro/print1through10/print1through10.c'],
  ['increment-ints',         'intro/increment-ints/increment-ints.c'],
  ['clear',                  'intro/clear/clear.c'],
  ['yes',                    'intro/yes/yes.c'],
  # ---- algorithms: familiar algorithms in asm ----
  ['fizzbuzz',               'algorithms/fizzbuzz/fizzbuzz.c'],
  # bubble-sort ladder (tasks/incremental-example-versions.md): rung 1 sorts a
  # fixed array; rung 2 (the finished demo) adds reading ints from stdin.
  ['bubble-sort-1-fixed',    'algorithms/bubble-sort/bubble-sort-1-fixed.c'],
  ['bubble-sort',            'algorithms/bubble-sort/bubble-sort.c'],
  # unit tests for the pure sort (tasks/separate-io-from-algorithm-remaining-demos.md)
  ['bubble-sort-core-test',  'algorithms/bubble-sort/bubble-sort-core-test.c'],
  ['pascals-triangle',       'algorithms/pascals-triangle/pascals-triangle.c'],
  ['sieve',                  'algorithms/sieve/sieve.c'],
  ['binary-search',          'algorithms/binary-search/binary-search.c'],
  # rpn ladder — incremental teaching rungs leading to rpn
  # (tasks/rpn-incremental-versions.md)
  ['rpn-1-stack',            'algorithms/rpn/rpn-1-stack.c'],
  ['rpn-2-ops',              'algorithms/rpn/rpn-2-ops.c'],
  ['rpn-3-float',            'algorithms/rpn/rpn-3-float.c'],
  ['rpn',                    'algorithms/rpn/rpn.c'],
  # I/O-separated variant + its unit tests (tasks/examples-separate-io-from-algorithm.md)
  ['rpn-eval',               'algorithms/rpn/rpn-eval.c'],
  ['rpn-eval-test',          'algorithms/rpn/rpn-eval-test.c'],
  # ---- lang: little languages (infix parsers / evaluators) ----
  # calc ladder — incremental teaching rungs leading to calc-sdt
  # (tasks/calc-incremental-versions.md)
  ['calc-1-numbers',         'lang/calc/calc-1-numbers.c'],
  ['calc-2-addsub',          'lang/calc/calc-2-addsub.c'],
  ['calc-3-muldiv',          'lang/calc/calc-3-muldiv.c'],
  ['calc-4-parens',          'lang/calc/calc-4-parens.c'],
  ['calc-5-neg',             'lang/calc/calc-5-neg.c'],
  ['calc-6-float',           'lang/calc/calc-6-float.c'],
  ['calc-sdt',               'lang/calc/calc-sdt.c'],
  ['calc-tree',              'lang/calc/calc-tree.c'],
  # I/O-separated variant + its unit tests (tasks/examples-separate-io-from-algorithm.md)
  ['calc-eval',              'lang/calc/calc-eval.c'],
  ['calc-eval-test',         'lang/calc/calc-eval-test.c'],
  # ---- transforms: stdin -> stdout stream transforms ----
  ['wc',                     'transforms/wc/wc.c'],
  # head ladder (tasks/incremental-example-versions.md): rung 1 is the fixed-N
  # stdin filter; rung 2 (the finished demo) adds argv (-n flag + FILE).
  ['head-1-count',           'transforms/head/head-1-count.c'],
  ['head',                   'transforms/head/head.c'],
  ['rev',                    'transforms/rev/rev.c'],
  ['tr',                     'transforms/tr/tr.c'],
  ['rot13',                  'transforms/rot13/rot13.c'],
  ['expand',                 'transforms/expand/expand.c'],
  ['uniq',                   'transforms/uniq/uniq.c'],
  ['nl',                     'transforms/nl/nl.c'],
  ['cut',                    'transforms/cut/cut.c'],
  ['od',                     'transforms/od/od.c'],
  ['tac',                    'transforms/tac/tac.c'],
  ['tail',                   'transforms/tail/tail.c'],
  ['base64',                 'transforms/base64/base64.c'],
  ['strings',                'transforms/strings/strings.c'],
  # ---- fileio: open/close/read/write of named files ----
  ['cat',                    'fileio/cat/cat.c'],
  ['nologin',                'fileio/nologin/nologin.c'],
  # cksum ladder (tasks/reference/incremental-example-decomposition.md): rung 1
  # is the CRC32 algorithm over stdin; rung 2 (finished) adds file-or-stdin.
  ['cksum-1-stdin',          'fileio/cksum/cksum-1-stdin.c'],
  ['cksum',                  'fileio/cksum/cksum.c'],
  # I/O-separated variant + its unit tests (tasks/separate-io-from-algorithm-remaining-demos.md)
  ['cksum-core',             'fileio/cksum/cksum-core.c'],
  ['cksum-core-test',        'fileio/cksum/cksum-core-test.c'],
  ['touch',                  'fileio/touch/touch.c'],
  ['cp',                     'fileio/cp/cp.c'],
  ['comm',                   'fileio/comm/comm.c'],
  # ---- arguments: argv handling, atoi, variable argc ----
  ['echo',                   'arguments/echo/echo.c'],
  ['factorial',              'arguments/factorial/factorial.c'],
  ['gcd',                    'arguments/gcd/gcd.c'],
  ['tee',                    'arguments/tee/tee.c'],
  ['seq',                    'arguments/seq/seq.c'],
  ['factor',                 'arguments/factor/factor.c'],
  # ---- recursion: per-call stack frames ----
  ['get-char-from-user',     'recursion/get-char-from-user/get-char-from-user.c'],
  ['fibonacci',              'recursion/fibonacci/fibonacci.c'],
  ['hanoi',                  'recursion/hanoi/hanoi.c'],
  ['queens',                 'recursion/queens/queens.c'],
  # ---- extras (not in main reading order) ----
  ['print-out-ascii',        'extras/print-out-ascii/print-out-ascii.c'],
  ['commaAndPeriodCounter',  'extras/commaAndPeriodCounter/commaAndPeriodCounter.c'],
  ['subrountines',           'extras/subrountines/subrountines.c'],
  ['testStringsForEquality', 'extras/testStringsForEquality/testStringsForEquality.c'],
]

foreach demo : demos
  executable('spimulator-example-' + demo[0],
    demo[1],
    include_directories: inc,
    link_with: io_lib,
    c_args: edu_args,
    link_args: edu_link_args,
    install: true,
    install_dir: example_install_dir,
  )
endforeach

# Library demos — pair a tiny demo program with its library.
# Each entry: [target_name, source_path, link_with_lib, extra_includes]
lib_demos = [
  ['ctype-demo', 'lib/libctype-demo/ctype-demo.c', libctype_lib, libctype_inc],
  ['str-demo', 'lib/libstr-demo/str-demo.c', libstr_lib, libstr_inc],
]

foreach demo : lib_demos
  executable('spimulator-example-' + demo[0],
    demo[1],
    include_directories: [inc, demo[3]],
    link_with: [io_lib, demo[2]],
    c_args: edu_args,
    link_args: edu_link_args,
    install: true,
    install_dir: example_install_dir,
  )
endforeach

# libstdlib demos need both libstdlib AND libctype linked
# (atoi calls into isspace/isdigit).
executable('spimulator-example-atoi-demo',
  'lib/libstdlib-demo/atoi-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  c_args: edu_args,
  link_args: edu_link_args,
  install: true,
  install_dir: example_install_dir,
)

# abs/labs don't depend on libctype — leaf functions — but link
# both libs for consistency with atoi-demo and any future
# composite demo.
executable('spimulator-example-abs-demo',
  'lib/libstdlib-demo/abs-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  c_args: edu_args,
  link_args: edu_link_args,
  install: true,
  install_dir: example_install_dir,
)

executable('spimulator-example-exit-demo',
  'lib/libstdlib-demo/exit-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  c_args: edu_args,
  link_args: edu_link_args,
  install: true,
  install_dir: example_install_dir,
)

executable('spimulator-example-bsearch-demo',
  'lib/libstdlib-demo/bsearch-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  c_args: edu_args,
  link_args: edu_link_args,
  install: true,
  install_dir: example_install_dir,
)

executable('spimulator-example-atexit-demo',
  'lib/libstdlib-demo/atexit-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  c_args: edu_args,
  link_args: edu_link_args,
  install: true,
  install_dir: example_install_dir,
)

# ---------------------------------------------------------------------------
# Library-demo tests.  Each test runs both the C binary AND the asm version
# under spim, diffs both stdouts against the pinned `.expected` golden, and
# (for exit-demo and atexit-demo) checks the host shell exit status against
# `.expected-status`.  Mirrors the pattern in /spimulator/tests/.
# ---------------------------------------------------------------------------
demo_test_runner = files('../tests/run-demo.sh')

# `spimulator_exe` and `exception_file` come from the parent project's
# meson.build via subdir scope.
lib_demo_tests = [
  'ctype-demo', 'str-demo', 'atoi-demo', 'abs-demo', 'exit-demo', 'bsearch-demo',
  'atexit-demo',
  # Regular demos with pinned goldens (stdin-driven; see run-demo.sh)
  'strings', 'bubble-sort-1-fixed', 'bubble-sort', 'bubble-sort-core-test',
  'head-1-count', 'head', 'wc',
  'cksum-1-stdin', 'cksum', 'cksum-core', 'cksum-core-test',
  'rpn-1-stack', 'rpn-2-ops', 'rpn-3-float', 'rpn', 'rpn-eval', 'rpn-eval-test',
  'calc-1-numbers', 'calc-2-addsub', 'calc-3-muldiv',
  'calc-4-parens', 'calc-5-neg', 'calc-6-float', 'calc-sdt', 'calc-tree', 'calc-eval',
  'calc-eval-test',
]
foreach name : lib_demo_tests
  test(name,
    demo_test_runner,
    args: [
      meson.current_build_dir(),
      spimulator_exe.full_path(),
      exception_file,
      name,
    ],
    suite: 'examples',
  )
endforeach
