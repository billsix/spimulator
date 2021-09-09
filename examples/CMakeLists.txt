cmake_minimum_required(VERSION 3.6)

project (examples LANGUAGES C)

add_library(platformabstraction STATIC platformabstraction/src/platformabstraction.c platformabstraction/include/platformabstraction.h)

include_directories(platformabstraction/include/)

add_executable(example01 01-print1through10/src/example01.c)
set_property(TARGET example01 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example01 platformabstraction )
install(TARGETS example01 DESTINATION bin)

add_executable(example01-2 01-print1through10/src/example01-2.c)
set_property(TARGET example01-2 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example01-2 platformabstraction )
install(TARGETS example01-2 DESTINATION bin)

add_executable(example01-3 01-print1through10/src/example01-2.c)
set_property(TARGET example01-3 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example01-3 platformabstraction )
install(TARGETS example01-3 DESTINATION bin)


add_executable(example02 02-increment-ints/src/example02.c)
set_property(TARGET example02 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example02 platformabstraction )
install(TARGETS example02 DESTINATION bin)

add_executable(example03 03-get-char-from-user/src/example03.c)
set_property(TARGET example03 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example03 platformabstraction )
install(TARGETS example03 DESTINATION bin)
