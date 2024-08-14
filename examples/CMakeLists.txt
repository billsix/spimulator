cmake_minimum_required(VERSION 3.6)

project (examples LANGUAGES C)

add_library(operatingsystemfunctions STATIC operatingsystemfunctions/src/operatingsystemfunctions.c operatingsystemfunctions/include/operatingsystemfunctions.h)

include_directories(operatingsystemfunctions/include/)

add_executable(01-helloworld-1 01-helloworld/01-helloworld-1.c)
set_property(TARGET 01-helloworld-1 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 01-helloworld-1 operatingsystemfunctions )
install(TARGETS 01-helloworld-1 DESTINATION bin)

add_executable(01-helloworld-2 01-helloworld/01-helloworld-2.c)
set_property(TARGET 01-helloworld-2 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 01-helloworld-2 operatingsystemfunctions )
install(TARGETS 01-helloworld-2 DESTINATION bin)

add_executable(01-helloworld-3 01-helloworld/01-helloworld-3.c)
set_property(TARGET 01-helloworld-3 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 01-helloworld-3 operatingsystemfunctions )
install(TARGETS 01-helloworld-3 DESTINATION bin)



add_executable(02-print1through10-1 02-print1through10/02-print1through10-1.c)
set_property(TARGET 02-print1through10-1 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 02-print1through10-1 operatingsystemfunctions )
install(TARGETS 02-print1through10-1 DESTINATION bin)

add_executable(02-print1through10-2 02-print1through10/02-print1through10-2.c)
set_property(TARGET 02-print1through10-2 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 02-print1through10-2 operatingsystemfunctions )
install(TARGETS 02-print1through10-2 DESTINATION bin)

add_executable(02-print1through10-3 02-print1through10/02-print1through10-3.c)
set_property(TARGET 02-print1through10-3 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 02-print1through10-3 operatingsystemfunctions )
install(TARGETS 02-print1through10-3 DESTINATION bin)




add_executable(03-increment-ints-1 03-increment-ints/03-increment-ints-1.c)
set_property(TARGET 03-increment-ints-1 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 03-increment-ints-1 operatingsystemfunctions )
install(TARGETS 03-increment-ints-1 DESTINATION bin)

add_executable(03-increment-ints-2 03-increment-ints/03-increment-ints-2.c)
set_property(TARGET 03-increment-ints-2 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 03-increment-ints-2 operatingsystemfunctions )
install(TARGETS 03-increment-ints-2 DESTINATION bin)

add_executable(03-increment-ints-3 03-increment-ints/03-increment-ints-3.c)
set_property(TARGET 03-increment-ints-3 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 03-increment-ints-3 operatingsystemfunctions )
install(TARGETS 03-increment-ints-3 DESTINATION bin)


add_executable(04-get-char-from-user-1 04-get-char-from-user/04-get-char-from-user-1.c)
set_property(TARGET 04-get-char-from-user-1 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 04-get-char-from-user-1 operatingsystemfunctions )
install(TARGETS 04-get-char-from-user-1 DESTINATION bin)

add_executable(04-get-char-from-user-2 04-get-char-from-user/04-get-char-from-user-2.c)
set_property(TARGET 04-get-char-from-user-2 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 04-get-char-from-user-2 operatingsystemfunctions )
install(TARGETS 04-get-char-from-user-2 DESTINATION bin)

add_executable(04-get-char-from-user-3 04-get-char-from-user/04-get-char-from-user-3.c)
set_property(TARGET 04-get-char-from-user-3 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 04-get-char-from-user-3 operatingsystemfunctions )
install(TARGETS 04-get-char-from-user-3 DESTINATION bin)

add_executable(05-print-out-ascii-1 05-print-out-ascii/05-print-out-ascii-1.c)
set_property(TARGET 05-print-out-ascii-1 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 05-print-out-ascii-1 operatingsystemfunctions )
install(TARGETS 05-print-out-ascii-1 DESTINATION bin)

add_executable(05-print-out-ascii-2 05-print-out-ascii/05-print-out-ascii-2.c)
set_property(TARGET 05-print-out-ascii-2 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 05-print-out-ascii-2 operatingsystemfunctions )
install(TARGETS 05-print-out-ascii-2 DESTINATION bin)

add_executable(05-print-out-ascii-3 05-print-out-ascii/05-print-out-ascii-3.c)
set_property(TARGET 05-print-out-ascii-3 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 05-print-out-ascii-3 operatingsystemfunctions )
install(TARGETS 05-print-out-ascii-3 DESTINATION bin)

add_executable(06-commaAndPeriodCounter-1 06-commaAndPeriodCounter/06-commaAndPeriodCounter-1.c)
set_property(TARGET 06-commaAndPeriodCounter-1 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 06-commaAndPeriodCounter-1 operatingsystemfunctions )
install(TARGETS 06-commaAndPeriodCounter-1 DESTINATION bin)

add_executable(06-commaAndPeriodCounter-2 06-commaAndPeriodCounter/06-commaAndPeriodCounter-2.c)
set_property(TARGET 06-commaAndPeriodCounter-2 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 06-commaAndPeriodCounter-2 operatingsystemfunctions )
install(TARGETS 06-commaAndPeriodCounter-2 DESTINATION bin)

add_executable(06-commaAndPeriodCounter-3 06-commaAndPeriodCounter/06-commaAndPeriodCounter-3.c)
set_property(TARGET 06-commaAndPeriodCounter-3 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 06-commaAndPeriodCounter-3 operatingsystemfunctions )
install(TARGETS 06-commaAndPeriodCounter-3 DESTINATION bin)

add_executable(07-subrountines-1 07-subrountines/07-subrountines-1.c)
set_property(TARGET 07-subrountines-1 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 07-subrountines-1 operatingsystemfunctions )
install(TARGETS 07-subrountines-1 DESTINATION bin)

add_executable(07-subrountines-2 07-subrountines/07-subrountines-2.c)
set_property(TARGET 07-subrountines-2 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 07-subrountines-2 operatingsystemfunctions )
install(TARGETS 07-subrountines-2 DESTINATION bin)

add_executable(07-subrountines-3 07-subrountines/07-subrountines-3.c)
set_property(TARGET 07-subrountines-3 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 07-subrountines-3 operatingsystemfunctions )
install(TARGETS 07-subrountines-3 DESTINATION bin)




add_executable(08-testStringsForEquality-1 08-testStringsForEquality/08-testStringsForEquality-1.c)
set_property(TARGET 08-testStringsForEquality-1 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 08-testStringsForEquality-1 operatingsystemfunctions )
install(TARGETS 08-testStringsForEquality-1 DESTINATION bin)

add_executable(08-testStringsForEquality-2 08-testStringsForEquality/08-testStringsForEquality-2.c)
set_property(TARGET 08-testStringsForEquality-2 PROPERTY C_STANDARD 11)
TARGET_LINK_LIBRARIES( 08-testStringsForEquality-2 operatingsystemfunctions )
install(TARGETS 08-testStringsForEquality-2 DESTINATION bin)
