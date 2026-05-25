# examples/src/Makefile — generate clean, readable native assembly
# listings from every C demo.
#
# For each <demo>.c this emits <demo>.s: the host compiler's
# translation of the C to the BUILD HOST's native assembly
# (x86_64 on an x86_64 host, AArch64 on an arm host, ...).  No
# --target override -- `cc -S` follows the host, exactly like
# /pgu/src/c/Makefile's default `make`.
#
# Listings-only build: it never links or runs anything.  The
# runnable MIPS binaries + their tests are owned by meson
# (../../meson.build via this dir's meson.build, and
# ../tests/run-demo.sh).  This Makefile exists only to materialize
# the "what does a real compiler emit from this C?" artifact beside
# each .c / hand-written .asm pair, so a student can read all three
# vocabularies (C, hand-written MIPS, compiler-native asm) in one
# directory.
#
#   make            # generate <demo>.s for every C demo, native arch
#   make CC=clang    # ... using clang (what the container uses)
#   make clean       # remove the generated listings

CC ?= cc

# Educational flags -- keep the generated .s book-readable: no
# frame pointer, no CFI/unwind tables, no stack canaries.
# -fverbose-asm annotates each instruction with its C source line
# + variable names.  Same set meson uses in edu_args (note: NOT
# gcc's -fno-dwarf2-cfi-asm, which clang rejects).
EDU_FLAGS = -O0 -S \
            -fomit-frame-pointer \
            -fno-asynchronous-unwind-tables \
            -fno-unwind-tables \
            -fno-stack-protector \
            -fverbose-asm

# Demos include io.h/os.h/crt0.h via -I.; the lib demos also need
# their library headers.
INCLUDES = -I. -Ilib/libctype -Ilib/libstdlib

# Every C translation unit gets a listing -- demos AND the
# io/libctype/libstdlib helpers.  Each .c compiles on its own (-S,
# no link), so demo-vs-library doesn't matter here.
CSRC := $(shell find . -name '*.c')
LISTINGS := $(CSRC:.c=.s)

.PHONY: all listings clean
all: listings
listings: $(LISTINGS)

%.s: %.c
	$(CC) $(EDU_FLAGS) $(INCLUDES) $< -o $@

clean:
	rm -f $(LISTINGS)
