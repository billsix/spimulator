cmake_minimum_required(VERSION 3.13)

project(examples LANGUAGES C)

# ---------------------------------------------------------------------------
# Each demo is a freestanding C program that defines its own _start and
# reaches the kernel through the inline-asm syscall wrappers in os.h.  No
# libc.  The companion .asm in each demo directory is the hand-written MIPS
# version for spim — these C files exist so the demos can be run natively
# (Linux) and read alongside their assembly.
#
# Educational flags keep the generated .s book-readable when a student
# invokes `clang -S` themselves (no frame pointer, no CFI/unwind tables, no
# stack canaries).
# ---------------------------------------------------------------------------

set(EDUCATIONAL_C_FLAGS
    -O0
    -fomit-frame-pointer
    -fno-asynchronous-unwind-tables
    -fno-unwind-tables
    -fno-stack-protector
)

# ---------------------------------------------------------------------------
# IO helpers — small bridge over os.h.  Each helper is one .c file the
# student can read end-to-end and see the underlying os_write / os_read.
# ---------------------------------------------------------------------------
add_library(examples_io STATIC
    count-chars.c
    integer-to-string.c
    print-string.c
    print-int.c
    print-char.c
    read-char.c
)
target_compile_options(examples_io PRIVATE ${EDUCATIONAL_C_FLAGS})
target_include_directories(examples_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET examples_io PROPERTY C_STANDARD 11)

# ---------------------------------------------------------------------------
# Demo helper.  Each demo links against examples_io and is built -nostdlib.
# ---------------------------------------------------------------------------
function(add_demo name source)
    add_executable(${name} ${source})
    set_property(TARGET ${name} PROPERTY C_STANDARD 11)
    target_compile_options(${name} PRIVATE ${EDUCATIONAL_C_FLAGS})
    target_link_options(${name} PRIVATE -nostdlib)
    target_link_libraries(${name} PRIVATE examples_io)
    install(TARGETS ${name} DESTINATION bin)
endfunction()

add_demo(01-helloworld-1               01-helloworld/01-helloworld-1.c)
add_demo(02-print1through10-1          02-print1through10/02-print1through10-1.c)
add_demo(03-increment-ints-1           03-increment-ints/03-increment-ints-1.c)
add_demo(04-get-char-from-user-1       04-get-char-from-user/04-get-char-from-user-1.c)
add_demo(05-print-out-ascii-1          05-print-out-ascii/05-print-out-ascii-1.c)
add_demo(06-commaAndPeriodCounter-1    06-commaAndPeriodCounter/06-commaAndPeriodCounter-1.c)
add_demo(07-subrountines-1             07-subrountines/07-subrountines-1.c)
add_demo(08-testStringsForEquality-1   08-testStringsForEquality/08-testStringsForEquality-1.c)
