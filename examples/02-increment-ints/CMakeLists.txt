cmake_minimum_required(VERSION 3.6)

project (example02 LANGUAGES C)

add_executable(example02 src/example02.c)
# To make visual studio open with the correct project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT example02)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY XCODE_STARTUP_PROJECT example02)

# build using the C11 standard
set_property(TARGET example02 PROPERTY C_STANDARD 11)

install(TARGETS example02 DESTINATION bin)
