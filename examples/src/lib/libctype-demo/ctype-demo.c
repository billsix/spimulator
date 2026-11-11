/* ctype-demo — exercise every libctype function across the
 * printable-ASCII range (32..126).  One row per byte.  The
 * output IS the golden file: deterministic, easy to diff
 * against the asm-side run under spim.
 *
 * Format per row:
 *   c=NNN '<char>'  alpha=X alnum=X digit=X upper=X lower=X space=X up='<char>'
 * lo='<char>'
 *
 * Skips 0..31 and 127 (non-printable) so print_char doesn't emit
 * control bytes into the golden — exercises the functions just
 * the same.  isspace covers '\t'..'\r' (which are skipped) but
 * also ' ' (which is in range), so it still gets tested.
 */

/* clang-format off */
/* BUILD AND RUN, from this file's directory:
 *
 *   native Linux build, then run:
 *     $ gcc -std=gnu23 -nostdlib -I../.. -I../libctype -o ctype-demo ctype-demo.c ../libctype/libctype.c ../../print-char.c ../../print-int.c ../../print-string.c ../../count-chars.c
 *     $ ./ctype-demo
 *
 *   the paired MIPS assembly, in spimulator:
 *     $ spimulator -f ../libctype/libctype.asm -f ctype-demo.asm
 *
 *   the same assembly, in the Python port:
 *     $ spimulator-python -f ../libctype/libctype.asm -f ctype-demo.asm
 *
 *   the same assembly, in the GHC Haskell port:
 *     $ spimulator-haskell -f ../libctype/libctype.asm -f ctype-demo.asm
 */
/* clang-format on */

#include "io.h"
#include "libctype.h"

__attribute__((noreturn)) void _start(void) {
  for (int c = 32; c <= 126; c++) {
    print_string("c=");
    print_int(c);
    print_string(" '");
    print_char((char)c);
    print_string("'  alpha=");
    print_int(isalpha(c));
    print_string(" alnum=");
    print_int(isalnum(c));
    print_string(" digit=");
    print_int(isdigit(c));
    print_string(" upper=");
    print_int(isupper(c));
    print_string(" lower=");
    print_int(islower(c));
    print_string(" space=");
    print_int(isspace(c));
    print_string("  up='");
    print_char((char)toupper(c));
    print_string("' lo='");
    print_char((char)tolower(c));
    print_string("'\n");
  }
  os_exit(0);
}
