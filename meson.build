project('spimulator', 'c',
  version: '1.0',
  default_options: ['c_std=gnu23', 'warning_level=3']
)

# -Wno-zero-length-array: spim uses flexible array members.
# -Wunused-function: catches dead static fns like the late parse_br_imm32.
# -Wshadow: prevents inner-scope rebinding bugs.
# -Wimplicit-fallthrough=5: surfaces unannotated switch fallthroughs.
#   Level 5 means ONLY the C23 [[fallthrough]]; attribute is
#   accepted as an annotation — comment-style "/* fall through */"
#   doesn't count.  Fits the C23 modernization direction and
#   means there's one true way to express "I meant to flow into
#   the next case."  Anything without a break/return/goto/
#   [[fallthrough]] is a likely bug.
add_project_arguments(
  '-Wno-zero-length-array',
  '-Wunused-function',
  '-Wshadow',
  '-Wimplicit-fallthrough=5',
  language: 'c'
)


# BSD-licensed line-editing library. Auto by default; gate with
# -Dline_editing=disabled to skip if not packaged, or -Dline_editing=enabled
# to fail hard if missing.
libedit_dep = dependency('libedit', required: get_option('line_editing'))
if libedit_dep.found()
  add_project_arguments('-DHAVE_LIBEDIT=1', language: 'c')
endif

# Host endianness.  The simulator's mem_word is the host's int32_t, so
# byte-extraction shifts in mem.c / data.c / run.c (LWL/LWR/SWL/SWR) depend
# on the host's byte order.  Auto-detect rather than requiring the user to
# pass -DSPIM_BIGENDIAN by hand.
if host_machine.endian() == 'big'
  add_project_arguments('-DSPIM_BIGENDIAN', language: 'c')
endif


# Configure config.h
conf_data = configuration_data()
conf_data.set_quoted('DEFAULT_EXCEPTION_HANDLER',
                     get_option('prefix') / get_option('datadir') / 'spimulator/exceptions.s')

configure_file(
  input: 'include/config.h.in',
  output: 'config.h',
  configuration: conf_data
)

# Source files.  The hand-written parser became the sole parser in
# Phase 5 of the parser migration (2026-05-19); flex+bison and their
# `find_program` calls + custom_target rules were removed at the
# same time.  See tasks/handwritten-parser-migration.md.
source_files = files(
  'src/spim-utils.c',
  'src/run.c',
  'src/mem.c',
  'src/inst.c',
  'src/data.c',
  'src/sym-tbl.c',
  'src/syscall.c',
  'src/display-utils.c',
  'src/string-stream.c',
  'src/explain.c',
  'src/scanner.c',
  'src/parser.c',
  'src/pseudo_op.c',
  'src/asm_event.c',
  'src/ast.c'
)

spim_source_files = source_files + files('src/spim.c')

# Include directories
# Note: meson.current_build_dir() allows accessing generated files
inc = include_directories('include')

link_libraries = [
  meson.get_compiler('c').find_library('m')
]

# Executable
spimulator_exe = executable('spimulator',
  spim_source_files,
  include_directories: inc,
  dependencies: link_libraries + [libedit_dep],
  install: true
)

# Install data files
install_data('src/exceptions.s',
  install_dir: get_option('datadir') / 'spimulator'
)

# Install the man page. Meson uses the filename's trailing digit as the
# section number, so Documentation/spim.1 installs to share/man/man1/spim.1.
install_man('Documentation/spim.1')

# --- Regression tests ----------------------------------------------------
#
# All driven by tests/run-test.sh, which encodes the per-test invocation
# (some need stdin, some need argv, some check the exit code, some check
# substring output).  Run with `meson test -C builddir`.

test_driver = files('tests/run-test.sh')
exception_file = meson.current_source_dir() / 'src' / 'exceptions.s'

regression_tests = [
  # Standard "Passed all tests" sentinel
  'bare', 'core', 'le', 'dir', 'octal_escape', 'argv', 'read_int_eof',
  # Custom criteria
  'read_char_eof', 'args_cmd',
  # Unix-process conformance (exit codes)
  'return_value', 'unaligned', 'missing_main', 'parse_error', 'stderr_split',
  # Exception-handler paths
  'divide_by_zero',
  # Teaching-mode golden output + parity sanity
  'explain', 'explain_parity',
  # Assemble-time event trace (Phase 1 of parse-tree migration)
  'listing',
  # AST parser parity + print-ast
  'ast_parity', 'ast_parity_all', 'print_ast',
  # Pseudo-op AST wrapping
  'show_expansion',
]

foreach name : regression_tests
  test(name, test_driver,
       args: [spimulator_exe.full_path(), exception_file, name],
       suite: 'regression')
endforeach
