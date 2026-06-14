# Build the C ports of the book's assembly examples.
#
# Each .c is first compiled to a .s with educational flags so the
# generated assembly stays readable (no frame pointer, no CFI/unwind
# tables, no stack canaries).  The .s is then assembled and linked.
#
# Multi-arch: os.h handles per-arch syscall conventions, so the
# host arch determines the target by default.
#
# Override examples:
#   make                                # native arch
#   make EXTRA_CFLAGS=-m32              # i386 on x86_64 host
#   make EXTRA_CFLAGS=--target=aarch64-linux-gnu \
#        CC="clang --sysroot=/usr/aarch64-linux-gnu"
#                                       # cross compile to aarch64
#
# Supported arches (see os.h): i386, x86_64, arm, aarch64, mips,
# all on Linux.

CC          ?= clang
EXTRA_CFLAGS ?=

# Educational flags — keep generated .s book-readable.
GEN_FLAGS = $(EXTRA_CFLAGS) -S -O0 \
            -fomit-frame-pointer -fno-asynchronous-unwind-tables \
            -fno-unwind-tables -fno-dwarf2-cfi-asm -fno-stack-protector

LINK_FLAGS_NOSTDLIB = $(EXTRA_CFLAGS) -nostdlib
LINK_FLAGS_LIBC     = $(EXTRA_CFLAGS)

FREESTANDING = exit \
               maximum \
               helloworld-nolib \
               power \
               factorial \
               conversion-program \
               toupper-nomm-simplified \
               read-records \
               write-records \
               add-year

LIBC_PROGRAMS = helloworld-lib \
                printf-example

# Library .c files that have no _start of their own.  They get
# linked into the freestanding programs above, but we also list
# their .s files here as a `helpers` target so a stand-alone
# `make helpers` will compile them and surface any breakage in
# files that wouldn't otherwise be exercised.
HELPERS_S = count-chars.s \
            integer-to-string.s \
            write-newline.s \
            read-record.s \
            write-record.s \
            error-exit.s \
            alloc.s

all: $(FREESTANDING) $(LIBC_PROGRAMS) helpers

helpers: $(HELPERS_S)

# ---------- pattern rule: .c -> .s ----------------------------------
%.s: %.c
	$(CC) $(GEN_FLAGS) $< -o $@

# ---------- single-file freestanding programs -----------------------
exit: exit.s
	$(CC) $(LINK_FLAGS_NOSTDLIB) -o $@ $^

maximum: maximum.s
	$(CC) $(LINK_FLAGS_NOSTDLIB) -o $@ $^

helloworld-nolib: helloworld-nolib.s
	$(CC) $(LINK_FLAGS_NOSTDLIB) -o $@ $^

power: power.s
	$(CC) $(LINK_FLAGS_NOSTDLIB) -o $@ $^

factorial: factorial.s
	$(CC) $(LINK_FLAGS_NOSTDLIB) -o $@ $^

toupper-nomm-simplified: toupper-nomm-simplified.s
	$(CC) $(LINK_FLAGS_NOSTDLIB) -o $@ $^

# ---------- multi-file freestanding programs ------------------------
conversion-program: conversion-program.s count-chars.s integer-to-string.s write-newline.s
	$(CC) $(LINK_FLAGS_NOSTDLIB) -o $@ $^

read-records: read-records.s read-record.s count-chars.s write-newline.s
	$(CC) $(LINK_FLAGS_NOSTDLIB) -o $@ $^

write-records: write-records.s write-record.s
	$(CC) $(LINK_FLAGS_NOSTDLIB) -o $@ $^

add-year: add-year.s read-record.s write-record.s
	$(CC) $(LINK_FLAGS_NOSTDLIB) -o $@ $^

# ---------- libc programs -------------------------------------------
helloworld-lib: helloworld-lib.s
	$(CC) $(LINK_FLAGS_LIBC) -o $@ $^

printf-example: printf-example.s
	$(CC) $(LINK_FLAGS_LIBC) -o $@ $^

# ---------- check ---------------------------------------------------
# Run each binary and verify it does what the book chapter says.
# Exit codes / stdout are matched exactly.  Several programs return
# their result via $?, so we cannot use `set -e` here — each check
# explicitly bails on failure.
check: all
	@echo "=== exit (expect 0) ===" ; \
	./exit ; rc=$$? ; [ $$rc -eq 0   ] || { echo "FAIL exit=$$rc"; exit 1; } ; \
	echo "=== maximum (expect 222) ===" ; \
	./maximum ; rc=$$? ; [ $$rc -eq 222 ] || { echo "FAIL maximum=$$rc"; exit 1; } ; \
	echo "=== factorial (expect 24) ===" ; \
	./factorial ; rc=$$? ; [ $$rc -eq 24 ] || { echo "FAIL factorial=$$rc"; exit 1; } ; \
	echo "=== power (expect 33) ===" ; \
	./power ; rc=$$? ; [ $$rc -eq 33 ] || { echo "FAIL power=$$rc"; exit 1; } ; \
	echo "=== helloworld-nolib (expect 'hello world') ===" ; \
	out=`./helloworld-nolib` || { echo "FAIL helloworld-nolib"; exit 1; } ; \
	[ "$$out" = "hello world" ] || { echo "FAIL got '$$out'"; exit 1; } ; \
	echo "=== helloworld-lib (expect 'hello world') ===" ; \
	out=`./helloworld-lib` || { echo "FAIL helloworld-lib"; exit 1; } ; \
	[ "$$out" = "hello world" ] || { echo "FAIL got '$$out'"; exit 1; } ; \
	echo "=== conversion-program (expect '824') ===" ; \
	out=`./conversion-program` || { echo "FAIL conversion-program"; exit 1; } ; \
	[ "$$out" = "824" ] || { echo "FAIL got '$$out'"; exit 1; } ; \
	echo "=== printf-example ===" ; \
	out=`./printf-example` || { echo "FAIL printf-example"; exit 1; } ; \
	[ "$$out" = "Hello! Jonathan is a person who loves the number 3" ] \
	    || { echo "FAIL got '$$out'"; exit 1; } ; \
	echo "=== records suite (write/read/add-year round trip) ===" ; \
	./write-records || { echo "FAIL write-records"; exit 1; } ; \
	out=`./read-records | tr '\n' ' '` ; \
	[ "$$out" = "Fredrick Marilyn Derrick " ] \
	    || { echo "FAIL got '$$out'"; exit 1; } ; \
	./add-year || { echo "FAIL add-year"; exit 1; } ; \
	[ -s testout.dat ] || { echo "FAIL testout.dat empty"; exit 1; } ; \
	echo "=== toupper (expect 'HELLO WORLD') ===" ; \
	echo "hello world" > /tmp/pgu_in.txt ; \
	./toupper-nomm-simplified /tmp/pgu_in.txt /tmp/pgu_out.txt \
	    || { echo "FAIL toupper"; exit 1; } ; \
	out=`cat /tmp/pgu_out.txt` ; \
	[ "$$out" = "HELLO WORLD" ] || { echo "FAIL got '$$out'"; exit 1; } ; \
	rm -f /tmp/pgu_in.txt /tmp/pgu_out.txt ; \
	echo ; echo "ALL CHECKS PASSED"

# ---------- housekeeping --------------------------------------------
clean:
	rm -f $(FREESTANDING) $(LIBC_PROGRAMS) *.s test.dat testout.dat

.PHONY: all check clean helpers
