project('spimulator', 'c',
  version: '1.0',
  default_options: ['c_std=gnu11']
)

# Platform-specific configuration
if host_machine.system() == 'windows'
  add_project_arguments('-x', 'c++', language: 'c')
  add_project_arguments('-std=c++98', language: 'cpp')
endif

# Add compile definitions
add_project_arguments('-DIMGUI_IMPL_OPENGL_LOADER_GL3W=1', language: 'c')

# Configure config.h
conf_data = configuration_data()
conf_data.set_quoted('DEFAULT_EXCEPTION_HANDLER',
                     get_option('prefix') / get_option('datadir') / 'spimulator/exceptions.s')

configure_file(
  input: 'src/config.h.in',
  output: 'config.h',
  configuration: conf_data
)

# Source files
include_files = files(
  'src/data.h',
  'src/inst.h',
  'src/mem.h',
  'src/op.h',
  'src/parser.h',
  'src/reg.h',
  'src/run.h',
  'src/scanner.h',
  'src/spim.h',
  'src/spim-utils.h',
  'src/string-stream.h',
  'src/sym-tbl.h',
  'src/syscall.h',
  'src/parser_yacc.h'
)

source_files = files(
  'src/spim-utils.c',
  'src/lex.yy.c',
  'src/parser_yacc.c',
  'src/run.c',
  'src/mem.c',
  'src/inst.c',
  'src/data.c',
  'src/sym-tbl.c',
  'src/syscall.c',
  'src/display-utils.c',
  'src/string-stream.c'
)

spim_source_files = source_files + files('src/spim.c')

# Include directories
inc = include_directories('src', '.')

# Platform-specific libraries
if host_machine.system() == 'windows'
  link_libraries = [
    meson.get_compiler('c').find_library('wsock32'),
    meson.get_compiler('c').find_library('ws2_32')
  ]
else
  link_libraries = [
    meson.get_compiler('c').find_library('m')
  ]
endif

# Executable
spimulator_exe = executable('spimulator',
  spim_source_files,
  include_directories: inc,
  dependencies: link_libraries,
  install: true
)

# Install data files
install_data('src/exceptions.s',
  install_dir: get_option('datadir') / 'spimulator'
)
