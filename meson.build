project('spimulator', 'c',
  version: '1.0',
  default_options: ['c_std=gnu23', 'warning_level=3']
)

# Warning flags.  Probed per-compiler with has_argument() so an unknown spelling
# is dropped rather than failing the build under the core's -Werror — the image
# builds spim with clang (Dockerfile) while the host dev build is often gcc.
#   -Wno-zero-length-array : spim uses flexible array members.
#   -Wunused-function      : catches dead static fns like the late parse_br_imm32.
#   -Wshadow               : prevents inner-scope rebinding bugs.
#   -Wno-string-concatenation : clang only; explain.c's category/modifier teaching
#     tables deliberately concatenate a description across several adjacent string
#     literals, which clang's heuristic reads as a possible missing comma.
cc = meson.get_compiler('c')
foreach warg : [
    '-Wno-zero-length-array',
    '-Wunused-function',
    '-Wshadow',
    '-Wno-string-concatenation',
  ]
  if cc.has_argument(warg)
    add_project_arguments(warg, language: 'c')
  endif
endforeach
# -Wimplicit-fallthrough: surface unannotated switch fallthroughs.  GCC's level 5
# means ONLY the C23 [[fallthrough]] attribute counts (comment-style "/* fall
# through */" does not) — one true way to say "I meant to flow into the next
# case."  clang has only the plain form; use whichever the compiler accepts.
if cc.has_argument('-Wimplicit-fallthrough=5')
  add_project_arguments('-Wimplicit-fallthrough=5', language: 'c')
elif cc.has_argument('-Wimplicit-fallthrough')
  add_project_arguments('-Wimplicit-fallthrough', language: 'c')
endif


# BSD-licensed line-editing library. Auto by default; gate with
# -Dline_editing=disabled to skip if not packaged, or -Dline_editing=enabled
# to fail hard if missing.
libedit_dep = dependency('libedit', required: get_option('line_editing'))
if libedit_dep.found()
  add_project_arguments('-DHAVE_LIBEDIT=1', language: 'c')
endif

# Host endianness.  The simulator's mem_word is the host's int32_t, so
# byte-extraction shifts in memory.c / data.c / run.c (LWL/LWR/SWL/SWR)
# depend on the host's byte order.  Auto-detect rather than requiring
# the user to pass -DSPIM_BIGENDIAN by hand.
if host_machine.endian() == 'big'
  add_project_arguments('-DSPIM_BIGENDIAN', language: 'c')
endif


# Generate config.h from config.h.in.  Currently no substitutions
# are performed; the call is scaffolding for future build-time knobs.
configure_file(
  input: 'include/config.h.in',
  output: 'config.h',
  configuration: configuration_data()
)

# Source files.  The hand-written parser became the sole parser in
# Phase 5 of the parser migration (2026-05-19); flex+bison and their
# `find_program` calls + custom_target rules were removed at the
# same time.  See tasks/handwritten-parser-migration.md.
source_files = files(
  'src/spim-utils.c',
  'src/run.c',
  'src/memory.c',
  'src/instruction.c',
  'src/data.c',
  'src/symbol-table.c',
  'src/syscall.c',
  'src/display-utils.c',
  'src/string-stream.c',
  'src/explain.c',
  'src/scanner.c',
  'src/parser.c',
  'src/pseudo-op.c',
  'src/assembler-event.c',
  'src/ast.c'
)

spim_source_files = source_files + files('src/spim.c')

# Include directories
# Note: meson.current_build_dir() allows accessing generated files
inc = include_directories('include')

link_libraries = [
  meson.get_compiler('c').find_library('m')
]

# Executable.  -Werror is scoped to the simulator core here (not a global
# `werror=true`) so the teaching demos under examples/ — deliberately simple
# C read against generated asm, out of the modernization scope — are not gated
# by it.  Override with -Dcore_werror=false.
core_c_args = []
# Integer-conversion gate for the core: flag silent narrowing / sign-flip
# conversions (the simulator core was made clean under these 2026-10-16; see the
# integer-conversion-hardening task). Scoped to clang because the image and the
# sanitizer gates all build with clang (Dockerfile) and the core is clang-clean
# under these; gcc's -Wsign-conversion is stricter and flags ~46 additional edge
# cases not yet addressed, so enabling it there would break a host gcc build.
if cc.get_id() == 'clang'
  foreach warg : ['-Wconversion', '-Wsign-conversion']
    if cc.has_argument(warg)
      core_c_args += warg
    endif
  endforeach
endif
if get_option('core_werror')
  core_c_args += '-Werror'
endif
spimulator_exe = executable('spimulator',
  spim_source_files,
  include_directories: inc,
  c_args: core_c_args,
  dependencies: link_libraries + [libedit_dep],
  install: true
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
  'bare', 'core', 'le', 'dir', 'octal_escape', 'argv', 'multifile',
  'read_int_eof', 'read_int_space_sep',
  # Custom criteria
  'read_char_eof', 'args_cmd', 'disasm',
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

# --- Examples curriculum (paired C + MIPS-asm teaching demos) -----------
# Builds the demos and registers `examples`-suite tests that validate
# both the C and asm sides against pinned `.expected` goldens.
subdir('examples/src')
