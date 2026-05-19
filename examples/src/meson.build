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

# Demos can find io.h / os.h here.
inc = include_directories('.')

# One executable per demo.  Pairs are [target_name, source_path].
# Demos are numbered in engagement-first reading order
# (see ../READING-ORDER.md).  Parts 1-6 = entries 01-29.
# Entries 30-33 are extras kept on disk but not in the
# main reading order.
demos = [
  # ---- Part 0: the smallest possible program ----
  ['00-exit',                   '00-exit/00-exit.c'],
  # ---- Part 1: first contact ----
  ['01-helloworld',             '01-helloworld/01-helloworld.c'],
  ['02-print1through10',        '02-print1through10/02-print1through10.c'],
  ['03-increment-ints',         '03-increment-ints/03-increment-ints.c'],
  ['04-clear',                  '04-clear/04-clear.c'],
  ['05-yes',                    '05-yes/05-yes.c'],
  # ---- Part 2: algorithms you already know (no argv) ----
  ['06-fizzbuzz',               '06-fizzbuzz/06-fizzbuzz.c'],
  ['07-bubble-sort',            '07-bubble-sort/07-bubble-sort.c'],
  ['08-pascals-triangle',       '08-pascals-triangle/08-pascals-triangle.c'],
  ['09-sieve',                  '09-sieve/09-sieve.c'],
  # ---- Part 3: stdin byte loops ----
  ['10-wc',                     '10-wc/10-wc.c'],
  ['11-head',                   '11-head/11-head.c'],
  ['12-rev',                    '12-rev/12-rev.c'],
  ['13-tr',                     '13-tr/13-tr.c'],
  ['14-rot13',                  '14-rot13/14-rot13.c'],
  ['15-expand',                 '15-expand/15-expand.c'],
  # ---- Part 4: files ----
  ['16-cat',                    '16-cat/16-cat.c'],
  ['17-nologin',                '17-nologin/17-nologin.c'],
  ['18-cksum',                  '18-cksum/18-cksum.c'],
  # ---- Part 5: argv ----
  ['19-echo',                   '19-echo/19-echo.c'],
  ['20-factorial',              '20-factorial/20-factorial.c'],
  ['21-gcd',                    '21-gcd/21-gcd.c'],
  ['22-binary-search',          '22-binary-search/22-binary-search.c'],
  ['23-tee',                    '23-tee/23-tee.c'],
  # ---- Part 6: stack frames and recursion ----
  ['24-get-char-from-user',     '24-get-char-from-user/24-get-char-from-user.c'],
  ['25-fibonacci',              '25-fibonacci/25-fibonacci.c'],
  ['26-hanoi',                  '26-hanoi/26-hanoi.c'],
  ['27-queens',                 '27-queens/27-queens.c'],
  # ---- Part 7: deeper Unix tools ----
  ['28-seq',                    '28-seq/28-seq.c'],
  ['29-touch',                  '29-touch/29-touch.c'],
  ['30-factor',                 '30-factor/30-factor.c'],
  ['31-cp',                     '31-cp/31-cp.c'],
  ['32-uniq',                   '32-uniq/32-uniq.c'],
  ['33-nl',                     '33-nl/33-nl.c'],
  ['34-cut',                    '34-cut/34-cut.c'],
  ['35-od',                     '35-od/35-od.c'],
  ['36-tac',                    '36-tac/36-tac.c'],
  ['37-tail',                   '37-tail/37-tail.c'],
  ['38-comm',                   '38-comm/38-comm.c'],
  ['39-base64',                 '39-base64/39-base64.c'],
  # ---- Extras (not in main reading order) ----
  ['40-print-out-ascii',        '40-print-out-ascii/40-print-out-ascii.c'],
  ['41-commaAndPeriodCounter',  '41-commaAndPeriodCounter/41-commaAndPeriodCounter.c'],
  ['42-subrountines',           '42-subrountines/42-subrountines.c'],
  ['43-testStringsForEquality', '43-testStringsForEquality/43-testStringsForEquality.c'],
]

foreach demo : demos
  executable(demo[0],
    demo[1],
    include_directories: inc,
    link_with: io_lib,
    install: true,
  )
endforeach
