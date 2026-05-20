project('spimulator', 'c',
  version: '1.0',
  default_options: ['c_std=gnu23']
)

add_project_arguments('-Wno-zero-length-array', language: 'c')


# BSD-licensed line-editing library. Auto by default; gate with
# -Dline_editing=disabled to skip on platforms where it isn't packaged
# (e.g. Windows), or -Dline_editing=enabled to fail hard if missing.
libedit_dep = dependency('libedit', required: get_option('line_editing'))
if libedit_dep.found()
  add_project_arguments('-DHAVE_LIBEDIT=1', language: 'c')
endif

# Platform-specific configuration
if host_machine.system() == 'windows'
  add_project_arguments('-x', 'c++', language: 'c')
  add_project_arguments('-std=c++98', language: 'cpp')
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
  'src/pseudo_op.c'
)

spim_source_files = source_files + files('src/spim.c')

# Include directories
# Note: meson.current_build_dir() allows accessing generated files
inc = include_directories('include')

# Platform-specific libraries.  spim has no socket code, so Windows
# only needs the standard runtime; Unix needs libm for the FP syscalls.
if host_machine.system() == 'windows'
  link_libraries = []
else
  link_libraries = [
    meson.get_compiler('c').find_library('m')
  ]
endif

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
