cmake_minimum_required(VERSION 3.6)

project (examples LANGUAGES C)

add_library(platformabstraction STATIC platformabstraction/src/platformabstraction.c platformabstraction/include/platformabstraction.h)

include_directories(platformabstraction/include/)

add_executable(example01 01-helloworld/example01.c)
set_property(TARGET example01 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example01 platformabstraction )
install(TARGETS example01 DESTINATION bin)

add_executable(example01-2 01-helloworld/example01-2.c)
set_property(TARGET example01-2 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example01-2 platformabstraction )
install(TARGETS example01-2 DESTINATION bin)

add_executable(example01-3 01-helloworld/example01-3.c)
set_property(TARGET example01-3 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example01-3 platformabstraction )
install(TARGETS example01-3 DESTINATION bin)



add_executable(example03 03-print1through10/example03.c)
set_property(TARGET example03 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example03 platformabstraction )
install(TARGETS example03 DESTINATION bin)

add_executable(example03-2 03-print1through10/example03-2.c)
set_property(TARGET example03-2 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example03-2 platformabstraction )
install(TARGETS example03-2 DESTINATION bin)

add_executable(example03-3 03-print1through10/example03-3.c)
set_property(TARGET example03-3 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example03-3 platformabstraction )
install(TARGETS example03-3 DESTINATION bin)


add_executable(example04 04-increment-ints/example04.c)
set_property(TARGET example04 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example04 platformabstraction )
install(TARGETS example04 DESTINATION bin)

add_executable(example05 05-get-char-from-user/example05.c)
set_property(TARGET example05 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( example05 platformabstraction )
install(TARGETS example05 DESTINATION bin)
