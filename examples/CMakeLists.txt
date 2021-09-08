cmake_minimum_required(VERSION 3.6)

project (examples LANGUAGES C)

add_library(iolib STATIC iolib/src/iolib.c iolib/include/iolib.h)

include_directories(iolib/include/)

add_executable(example01 01-print1through10/src/example01.c)
set_property(TARGET example01 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example01 iolib )
install(TARGETS example01 DESTINATION bin)

add_executable(example01-2 01-print1through10/src/example01-2.c)
set_property(TARGET example01-2 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example01-2 iolib )
install(TARGETS example01-2 DESTINATION bin)

add_executable(example01-3 01-print1through10/src/example01-2.c)
set_property(TARGET example01-3 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example01-3 iolib )
install(TARGETS example01-3 DESTINATION bin)


add_executable(example02 02-increment-ints/src/example02.c)
set_property(TARGET example02 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example02 iolib )
install(TARGETS example02 DESTINATION bin)

add_executable(example03 03-get-char-from-user/src/example03.c)
set_property(TARGET example03 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example03 iolib )
install(TARGETS example03 DESTINATION bin)
