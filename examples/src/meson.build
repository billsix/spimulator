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
  ),
)

# Demos can find io.h / os.h here.
inc = include_directories('.')

# One executable per demo.  Pairs are [target_name, source_path].
demos = [
  ['01-helloworld-1',             '01-helloworld/01-helloworld-1.c'],
  ['02-print1through10-1',        '02-print1through10/02-print1through10-1.c'],
  ['03-increment-ints-1',         '03-increment-ints/03-increment-ints-1.c'],
  ['04-get-char-from-user-1',     '04-get-char-from-user/04-get-char-from-user-1.c'],
  ['05-print-out-ascii-1',        '05-print-out-ascii/05-print-out-ascii-1.c'],
  ['06-commaAndPeriodCounter-1',  '06-commaAndPeriodCounter/06-commaAndPeriodCounter-1.c'],
  ['07-subrountines-1',           '07-subrountines/07-subrountines-1.c'],
  ['08-testStringsForEquality-1', '08-testStringsForEquality/08-testStringsForEquality-1.c'],
  ['09-clear-1',                  '09-clear/09-clear-1.c'],
  ['10-yes-1',                    '10-yes/10-yes-1.c'],
  ['11-cat-1',                    '11-cat/11-cat-1.c'],
  ['12-wc-1',                     '12-wc/12-wc-1.c'],
  ['13-head-1',                   '13-head/13-head-1.c'],
  ['14-rev-1',                    '14-rev/14-rev-1.c'],
  ['15-nologin-1',                '15-nologin/15-nologin-1.c'],
  ['16-tr-1',                     '16-tr/16-tr-1.c'],
  ['17-expand-1',                 '17-expand/17-expand-1.c'],
  ['18-cksum-1',                  '18-cksum/18-cksum-1.c'],
  ['19-echo-1',                   '19-echo/19-echo-1.c'],
  ['20-factorial-1',              '20-factorial/20-factorial-1.c'],
  ['21-cat-file-1',               '21-cat-file/21-cat-file-1.c'],
  ['22-gcd-1',                    '22-gcd/22-gcd-1.c'],
  ['23-head-file-1',              '23-head-file/23-head-file-1.c'],
  ['24-tee-1',                    '24-tee/24-tee-1.c'],
  ['25-fizzbuzz-1',               '25-fizzbuzz/25-fizzbuzz-1.c'],
  ['26-fibonacci-1',              '26-fibonacci/26-fibonacci-1.c'],
  ['27-binary-search-1',          '27-binary-search/27-binary-search-1.c'],
  ['28-bubble-sort-1',            '28-bubble-sort/28-bubble-sort-1.c'],
  ['29-rot13-1',                  '29-rot13/29-rot13-1.c'],
  ['30-hanoi-1',                  '30-hanoi/30-hanoi-1.c'],
  ['31-pascals-triangle-1',       '31-pascals-triangle/31-pascals-triangle-1.c'],
  ['32-sieve-1',                  '32-sieve/32-sieve-1.c'],
  ['33-queens-1',                 '33-queens/33-queens-1.c'],
]

foreach demo : demos
  executable(demo[0],
    demo[1],
    include_directories: inc,
    link_with: io_lib,
    install: true,
  )
endforeach
