project('spimulator', 'c',
  version: '1.0',
  default_options: ['c_std=gnu11']
)

# Find flex and bison
flex = find_program('flex', required: true)
bison = find_program('bison', required: true)

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
  input: 'include/config.h.in',
  output: 'config.h',
  configuration: conf_data
)

# Generate lexer from flex (output to src/)
lex_gen = custom_target('lex',
  input: 'src/scanner.l',
  output: 'lex.yy.c',
  command: [flex, '-o', '@OUTPUT@', '@INPUT@']
)

# Generate parser from bison
# Note: We'll generate both .c and .h files
parser_gen = custom_target('parser',
  input: 'src/parser.y',
  output: ['parser_yacc.c', 'parser_yacc.h'],
  command: [bison, '-d', '@INPUT@', '-o', '@OUTPUT0@']
)

# Source files (without the generated ones)
source_files = files(
  'src/spim-utils.c',
  'src/run.c',
  'src/mem.c',
  'src/inst.c',
  'src/data.c',
  'src/sym-tbl.c',
  'src/syscall.c',
  'src/display-utils.c',
  'src/string-stream.c'
)

# Add generated sources
spim_source_files = source_files + files('src/spim.c') + lex_gen + parser_gen

# Include directories
# Note: meson.current_build_dir() allows accessing generated files
inc = include_directories('include')

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
