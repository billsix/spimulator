project('spimulator-examples', 'c',
  version: '1.0',
  default_options: [
    'c_std=c11',
    'warning_level=1',
    'optimization=0',
  ]
)

# Educational flags — keep the generated .s book-readable.
# No frame pointer, no CFI/unwind tables, no stack canaries.
edu_args = [
  '-fomit-frame-pointer',
  '-fno-asynchronous-unwind-tables',
  '-fno-unwind-tables',
  '-fno-stack-protector',
]
add_project_arguments(edu_args, language: 'c')

# Each demo is freestanding — no libc, defines its own `_start`,
# reaches the kernel through inline-asm syscalls in os.h.
add_project_link_arguments('-nostdlib', language: 'c')

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
)

# libctype — ASCII character classification + case conversion.
# Adapted from musl libc src/ctype/.  See src/lib/libctype/LICENSE-musl.
libctype_lib = static_library('libctype',
  files('lib/libctype/libctype.c'),
)
libctype_inc = include_directories('lib/libctype')

# libstdlib — atoi (and, growing: abs, labs, bsearch, _Exit).
# Adapted from musl libc src/stdlib/ and src/exit/.
# See src/lib/libstdlib/LICENSE-musl.
libstdlib_lib = static_library('libstdlib',
  files('lib/libstdlib/libstdlib.c'),
  include_directories: libctype_inc,
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
    install: true,
  )
endforeach

# libstdlib demos need both libstdlib AND libctype linked
# (atoi calls into isspace/isdigit).
executable('atoi-demo',
  'lib/libstdlib-demo/atoi-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  install: true,
)

# abs/labs don't depend on libctype — leaf functions — but link
# both libs for consistency with atoi-demo and any future
# composite demo.
executable('abs-demo',
  'lib/libstdlib-demo/abs-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  install: true,
)

executable('exit-demo',
  'lib/libstdlib-demo/exit-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  install: true,
)

executable('bsearch-demo',
  'lib/libstdlib-demo/bsearch-demo.c',
  include_directories: [inc, libstdlib_inc, libctype_inc],
  link_with: [io_lib, libstdlib_lib, libctype_lib],
  install: true,
)
