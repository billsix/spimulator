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

#include "io.h"
#include "libstdlib.h"

__attribute__((noreturn)) void _start(void) {
  print_string("calling _Exit(42)\n");
  _Exit(42);
}
