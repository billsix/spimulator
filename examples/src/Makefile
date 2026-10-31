# examples/src/Makefile — readable native assembly listings AND
# runnable native executables for every C demo.
#
# Two artifacts per demo, mirroring /pgu/src/c/Makefile's flow:
#
#   <demo>.c  -->  <demo>.s  -->  bin/<demo>   (a runnable ELF)
#
#   1. <demo>.s : the host compiler's translation of the C to the
#      BUILD HOST's native assembly (x86_64 on an x86_64 host,
#      AArch64 on an arm host, ...), emitted *beside* the .c and
#      the hand-written MIPS <demo>.asm so a student can read all
#      three vocabularies of the same program in one directory.
#      `cc -S` with no --target follows the host.
#   2. bin/<demo> : that same .s assembled and linked into a
#      freestanding native executable (-nostdlib; the demo defines
#      its own _start and reaches the kernel through os.h).  The
#      point: the assembly you just read is literally what runs.
#
# This is the *native* teaching flow.  The MIPS binaries that run
# under spim (assembled from the hand-written <demo>.asm) plus
# their golden tests are owned by the meson build
# (../../meson.build via this dir's meson.build, ../tests/run-demo.sh)
# and are unaffected by anything here.
#
#   make             # listings + executables (native arch)
#   make listings     # just the <demo>.s files
#   make executables  # just the bin/<demo> binaries
#   make debug        # debuggable bin-debug/<demo> binaries (-g, for gdb/lldb)
#   make CC=clang ...  # pick the compiler (the container uses clang)
#   make clean        # remove listings, objects, archives, bin/, bin-debug/

CC ?= cc

# Educational flags -- keep the generated .s book-readable: no
# frame pointer, no CFI/unwind tables, no stack canaries.
# -fverbose-asm annotates each instruction with its C source line
# + variable names.  Same set meson uses in edu_args (note: NOT
# gcc's -fno-dwarf2-cfi-asm, which clang rejects).
# -std=gnu23 matches the meson build's project-wide `c_std=gnu23`
# so C23 keywords (true/false/bool/nullptr) resolve here too; the
# demos carry no <stdbool.h> by design, and clang defaults to C17.
EDU_FLAGS = -std=gnu23 \
            -O0 \
            -fomit-frame-pointer \
            -fno-asynchronous-unwind-tables \
            -fno-unwind-tables \
            -fno-stack-protector \
            -fverbose-asm

# Demos include io.h/os.h/crt0.h via -I.; the lib demos also need
# their library headers.
INCLUDES = -I. -Ilib/libctype -Ilib/libstdlib -Ilib/libstr

# Freestanding link: each demo defines its own _start and never
# touches libc.  Matches meson's edu_link_args.
LINK_FLAGS = -nostdlib

# Debug flags -- the OPPOSITE tradeoff from EDU_FLAGS: keep the frame
# pointer and emit DWARF (-g) so a student can set a breakpoint on
# my_main and step the C in gdb/lldb.  Still freestanding (own _start,
# -nostdlib).  These binaries are NOT meant to be read as assembly;
# the readable listing is the <demo>.s from EDU_FLAGS.  See the task
# tasks/examples-makefile-debug-build.md.
DEBUG_FLAGS = -std=gnu23 \
              -g \
              -O0 \
              -fno-omit-frame-pointer \
              -fno-stack-protector

# ---------------------------------------------------------------------------
# Source inventory.  Strip the leading ./ that `find` prepends so the
# top-level helper paths (wildcard, no ./) compare equal in filter-out.
# ---------------------------------------------------------------------------
ALL_C := $(patsubst ./%,%,$(shell find . -name '*.c'))

# Helpers have no _start of their own.  The small IO routines
# (print_string, read_int, ...) live directly in src/; the two
# library sources live under lib/.  Everything else is a demo with
# its own _start.
IO_C       := $(wildcard *.c)
LIBCTYPE_C := lib/libctype/libctype.c
LIBSTDLIB_C := lib/libstdlib/libstdlib.c
LIBSTR_C   := lib/libstr/libstr.c
HELPER_C   := $(IO_C) $(LIBCTYPE_C) $(LIBSTDLIB_C) $(LIBSTR_C)
DEMO_C     := $(filter-out $(HELPER_C),$(ALL_C))

# Parallel .s listing for *every* translation unit (demos + helpers).
ALL_S := $(ALL_C:.c=.s)

# One executable per demo, collected flat in bin/.  Demo basenames
# are unique across topic folders (meson relies on this too), so a
# flat bin/ never collides -- and a single `bin/` dir is trivially
# .gitignore'd, unlike extensionless binaries strewn beside sources.
EXES := $(addprefix bin/,$(notdir $(basename $(DEMO_C))))

# One debuggable executable per demo, collected flat in bin-debug/.
DEBUG_EXES := $(addprefix bin-debug/,$(notdir $(basename $(DEMO_C))))

# Helper objects, archived so the linker pulls only the members a
# given demo actually references -- which means every demo can link
# against all the archives without caring which it needs.
# Order at link time: libio (print_*), then libstdlib (atoi), then
# libctype (isspace, called by libstdlib), then libstr (string/mem
# primitives, used by str-demo; independent of the others).
IO_OBJ := $(IO_C:.c=.o)
ARCHIVES := libio.a libstdlib.a libstr.a libctype.a

# Parallel debug helper archives, compiled from the same .c with
# DEBUG_FLAGS so a student can step INTO print_string/atoi too.
IO_DEBUG_OBJ := $(IO_C:.c=.debug.o)
DEBUG_ARCHIVES := libio-debug.a libstdlib-debug.a libstr-debug.a libctype-debug.a

.PHONY: all listings executables debug clean
all: listings executables
listings: $(ALL_S)
executables: $(EXES)

# `make debug` is the SECOND, independent build: debuggable binaries
# in bin-debug/, compiled straight from the .c (no listing in between).
# It does not touch the readable listings/bin/ flow above.
debug: $(DEBUG_EXES)

# ---------- .c -> .s (the readable listing, beside the source) -------------
%.s: %.c
	$(CC) $(EDU_FLAGS) $(INCLUDES) -S $< -o $@

# ---------- .s -> .o (assemble the listing we just emitted) ----------------
%.o: %.s
	$(CC) -c $< -o $@

# ---------- helper archives ------------------------------------------------
libio.a: $(IO_OBJ)
	$(AR) rcs $@ $^

libctype.a: lib/libctype/libctype.o
	$(AR) rcs $@ $^

libstdlib.a: lib/libstdlib/libstdlib.o
	$(AR) rcs $@ $^

libstr.a: lib/libstr/libstr.o
	$(AR) rcs $@ $^

# ---------- demo executables ----------------------------------------------
# Locate each demo's .s by basename via vpath (the .s sits in the
# demo's own topic/name/ folder).  bin/<name> links that .s against
# the three helper archives.
vpath %.s $(sort $(dir $(DEMO_C)))

bin:
	mkdir -p bin

bin/%: %.s $(ARCHIVES) | bin
	$(CC) $(LINK_FLAGS) $< libio.a libstdlib.a libstr.a libctype.a -o $@

# ---------- debug build (bin-debug/, compiled from .c with -g) -------------
# Also resolve a demo's .c by basename so bin-debug/<name> finds it.
vpath %.c $(sort $(dir $(DEMO_C)))

# Debug helper objects + archives (straight from .c with DEBUG_FLAGS).
%.debug.o: %.c
	$(CC) $(DEBUG_FLAGS) $(INCLUDES) -c $< -o $@

libio-debug.a: $(IO_DEBUG_OBJ)
	$(AR) rcs $@ $^

libctype-debug.a: lib/libctype/libctype.debug.o
	$(AR) rcs $@ $^

libstdlib-debug.a: lib/libstdlib/libstdlib.debug.o
	$(AR) rcs $@ $^

libstr-debug.a: lib/libstr/libstr.debug.o
	$(AR) rcs $@ $^

bin-debug:
	mkdir -p bin-debug

# Compile and link each demo directly from its .c with -g into bin-debug/.
bin-debug/%: %.c $(DEBUG_ARCHIVES) | bin-debug
	$(CC) $(DEBUG_FLAGS) $(INCLUDES) $(LINK_FLAGS) $< \
	    libio-debug.a libstdlib-debug.a libstr-debug.a libctype-debug.a -o $@

clean:
	rm -f $(ALL_S) $(IO_OBJ) lib/libctype/libctype.o lib/libstdlib/libstdlib.o lib/libstr/libstr.o $(ARCHIVES)
	rm -f $(IO_DEBUG_OBJ) lib/libctype/libctype.debug.o lib/libstdlib/libstdlib.debug.o lib/libstr/libstr.debug.o $(DEBUG_ARCHIVES)
	rm -rf bin bin-debug
