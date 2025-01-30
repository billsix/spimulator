cmake_minimum_required(VERSION 3.6)

project (template LANGUAGES C)

add_executable(template src/template.c)

# build using the C11 standard
set_property(TARGET template PROPERTY C_STANDARD 11)

install(TARGETS template DESTINATION bin)
