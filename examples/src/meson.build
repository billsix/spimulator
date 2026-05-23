# examples/src — paired C + MIPS-asm teaching demos.  Invoked
# via `subdir('examples/src')` from /spimulator/meson.build.
# Standalone build is no longer supported; the merged project
# is the canonical entry point.

# Educational flags — keep the generated .s book-readable.
# No frame pointer, no CFI/unwind tables, no stack canaries.
# Scoped per-target (NOT add_project_arguments) so they don't
# leak into spim's compile, which has its own warning/std flags.
edu_args = [
  '-O0',
  '-fomit-frame-pointer',
  '-fno-asynchronous-unwind-tables',
  '-fno-unwind-tables',
  '-fno-stack-protector',
  # Emit a .s alongside each .o so students can compare the
  # compiler's translation of their C against the hand-written
  # MIPS .asm.  Lands at builddir/examples/src/<demo>.p/<demo>.c.s.
  # -fverbose-asm adds C-source variable-name annotations next to
  # each instruction — slight noise, big pedagogical gain.
  # See tasks/archive/PLAN-asm-listings.md.
  '-save-temps=obj',
  '-fverbose-asm',
]

# Each demo is freestanding — no libc, defines its own `_start`,
# reaches the kernel through inline-asm syscalls in os.h.
# Same scoping discipline: per-target, not project-wide
# (spim itself links against libc + libedit).
edu_link_args = ['-nostdlib']

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
  ['bubble-sort',            'algorithms/bubble-sort/bubble-sort.c'],
  ['pascals-triangle',       'algorithms/pascals-triangle/pascals-triangle.c'],
  ['sieve',                  'algorithms/sieve/sieve.c'],
  ['binary-search',          'algorithms/binary-search/binary-search.c'],
  # ---- transforms: stdin -> stdout stream transforms ----
  ['wc',                     'transforms/wc/wc.c'],
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
  # ---- fileio: open/close/read/write of named files ----
  ['cat',                    'fileio/cat/cat.c'],
  ['nologin',                'fileio/nologin/nologin.c'],
  ['cksum',                  'fileio/cksum/cksum.c'],
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
  executable(demo[0],
    demo[1],
    include_directories: inc,
    link_with: io_lib,
    c_args: edu_args,
    link_args: edu_link_args,
    install: true,
  )
endforeach

# Library demos — pair a tiny demo program with its library.
# Each entry: [target_name, source_path, link_with_lib, extra_includes]
lib_demos = [
  ['ctype-demo', 'lib/libctype-demo/ctype-demo.c', libctype_lib, libctype_inc],
]

foreach demo : lib_demos
  executable(demo[0],
    demo[1],
    include_directories: [inc, demo[3]],
    link_with: [io_lib, demo[2]],
    c_args: edu_args,
    link_args: edu_link_args,
    install: true,
  )
endforeach

# libstdlib demos need both libstdlib AND libctype linked
# (atoi calls into isspace/isdigit).
executable('atoi-demo',
  'lib/libstdlib-demo/atoi-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  c_args: edu_args,
  link_args: edu_link_args,
  install: true,
)

# abs/labs don't depend on libctype — leaf functions — but link
# both libs for consistency with atoi-demo and any future
# composite demo.
executable('abs-demo',
  'lib/libstdlib-demo/abs-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  c_args: edu_args,
  link_args: edu_link_args,
  install: true,
)

executable('exit-demo',
  'lib/libstdlib-demo/exit-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  c_args: edu_args,
  link_args: edu_link_args,
  install: true,
)

executable('bsearch-demo',
  'lib/libstdlib-demo/bsearch-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  c_args: edu_args,
  link_args: edu_link_args,
  install: true,
)

executable('atexit-demo',
  'lib/libstdlib-demo/atexit-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  c_args: edu_args,
  link_args: edu_link_args,
  install: true,
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
  'ctype-demo', 'atoi-demo', 'abs-demo', 'exit-demo', 'bsearch-demo',
  'atexit-demo',
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
