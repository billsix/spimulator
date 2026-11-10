/* exit-demo — show that _Exit propagates its argument to the
 * host shell's `$?`.
 *
 * Prints a one-line stdout message then calls _Exit(42).  The
 * paired asm version does the same.  Verification is two-part:
 *  1. stdout byte-identical between C and asm
 *  2. shell exit status == 42 in both
 *
 * Why 42: a non-zero, non-trivial value that's clearly not
 * "accidentally happened to work" (1, 0, or 255 could be).
 */

/* clang-format off */
/* BUILD AND RUN, from this file's directory:
 *
 *   native Linux build, then run:
 *     $ gcc -std=gnu23 -nostdlib -I../.. -I../libctype -I../libstdlib -o exit-demo exit-demo.c ../libstdlib/libstdlib.c ../../print-string.c ../libctype/libctype.c ../../count-chars.c
 *     $ ./exit-demo
 *
 *   the paired MIPS assembly, in spimulator:
 *     $ spimulator -f ../libctype/libctype.asm -f ../libstdlib/libstdlib.asm -f exit-demo.asm
 *
 *   the same assembly, in the Python port:
 *     $ spimulator-python -f ../libctype/libctype.asm -f ../libstdlib/libstdlib.asm -f exit-demo.asm
 *
 *   the same assembly, in the GHC Haskell port:
 *     $ spimulator-haskell -f ../libctype/libctype.asm -f ../libstdlib/libstdlib.asm -f exit-demo.asm
 */
/* clang-format on */

#include "io.h"
#include "libstdlib.h"

__attribute__((noreturn)) void _start(void) {
  print_string("calling _Exit(42)\n");
  _Exit(42);
}
