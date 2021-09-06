cmake_minimum_required(VERSION 3.6)

project (example03 LANGUAGES C)

add_executable(example03 src/example03.c)
# To make visual studio open with the correct project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT example03)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY XCODE_STARTUP_PROJECT example03)

# build using the C11 standard
set_property(TARGET example03 PROPERTY C_STANDARD 11)

install(TARGETS example03 DESTINATION bin)
