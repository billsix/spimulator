cmake_minimum_required(VERSION 3.6)

project (example01 LANGUAGES C)

add_executable(example01 src/example01.c)
# To make visual studio open with the correct project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT example01)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY XCODE_STARTUP_PROJECT example01)

# build using the C11 standard
set_property(TARGET example01 PROPERTY C_STANDARD 11)

install(TARGETS example01 DESTINATION bin)
