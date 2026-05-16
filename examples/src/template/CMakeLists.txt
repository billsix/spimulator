cmake_minimum_required(VERSION 3.13)

project(template LANGUAGES C)

# Standalone build of the demo template.  In tree, the template links
# against the parent examples_io static library; standalone, it pulls in
# the same helper sources directly.

set(EDUCATIONAL_C_FLAGS
    -O0
    -fomit-frame-pointer
    -fno-asynchronous-unwind-tables
    -fno-unwind-tables
    -fno-stack-protector
)

set(EXAMPLES_SRC ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(template
    src/template.c
    ${EXAMPLES_SRC}/count-chars.c
    ${EXAMPLES_SRC}/integer-to-string.c
    ${EXAMPLES_SRC}/print-string.c
    ${EXAMPLES_SRC}/print-int.c
    ${EXAMPLES_SRC}/print-char.c
    ${EXAMPLES_SRC}/read-char.c
)
set_property(TARGET template PROPERTY C_STANDARD 11)
target_include_directories(template PRIVATE ${EXAMPLES_SRC})
target_compile_options(template PRIVATE ${EDUCATIONAL_C_FLAGS})
target_link_options(template PRIVATE -nostdlib)

install(TARGETS template DESTINATION bin)
