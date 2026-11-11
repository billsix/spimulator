/* atexit-demo — register 3 cleanup handlers, then exit(42).
 *
 * The lesson here is the **order**: POSIX says atexit handlers
 * run in REVERSE registration order (LIFO).  We register h1
 * then h2 then h3, and the output shows them running in the
 * order 3, 2, 1.
 *
 * Verifies:
 *   1. stdout matches between C and asm
 *   2. host shell sees exit 42 (proves exit() called _Exit
 *      with the right argument, and exit handlers didn't
 *      somehow swallow it)
 */

/* clang-format off */
/* BUILD AND RUN, from this file's directory:
 *
 *   native Linux build, then run:
 *     $ gcc -std=gnu23 -nostdlib -I../.. -I../libctype -I../libstdlib -o atexit-demo atexit-demo.c ../libstdlib/libstdlib.c ../../print-string.c ../libctype/libctype.c ../../count-chars.c
 *     $ ./atexit-demo
 *
 *   the paired MIPS assembly, in spimulator:
 *     $ spimulator -f ../libctype/libctype.asm -f ../libstdlib/libstdlib.asm -f atexit-demo.asm
 *
 *   the same assembly, in the Python port:
 *     $ spimulator-python -f ../libctype/libctype.asm -f ../libstdlib/libstdlib.asm -f atexit-demo.asm
 *
 *   the same assembly, in the GHC Haskell port:
 *     $ spimulator-haskell -f ../libctype/libctype.asm -f ../libstdlib/libstdlib.asm -f atexit-demo.asm
 *
 *   the same assembly, in the Rust port:
 *     $ spimulator-rust -f ../libctype/libctype.asm -f ../libstdlib/libstdlib.asm -f atexit-demo.asm
 */
/* clang-format on */

#include "io.h"
#include "libstdlib.h"

static void h1(void) { print_string("handler 1 ran\n"); }
static void h2(void) { print_string("handler 2 ran\n"); }
static void h3(void) { print_string("handler 3 ran\n"); }

__attribute__((noreturn)) void _start(void) {
  atexit(h1);
  atexit(h2);
  atexit(h3);

  print_string("main about to exit\n");
  exit(42);
}
