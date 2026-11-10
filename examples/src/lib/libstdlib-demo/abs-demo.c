/* abs-demo — exercise libstdlib's absolute + labsolute over
 * representative int values, including the INT_MIN edge case
 * where -INT_MIN overflows back to INT_MIN.
 *
 * (libstdlib uses the longer names `absolute` / `labsolute`
 *  instead of the C library's `abs` / `labs`; see libstdlib.h
 *  for the rationale.)
 *
 * Same pattern as atoi-demo: table of inputs, loop, print one
 * line per case.  Output is the pinned golden (abs-demo.expected).
 */

/* clang-format off */
/* BUILD AND RUN, from this file's directory:
 *
 *   native Linux build, then run:
 *     $ gcc -std=gnu23 -nostdlib -I../.. -I../libctype -I../libstdlib -o abs-demo abs-demo.c ../libstdlib/libstdlib.c ../../print-char.c ../../print-int.c ../../print-string.c ../libctype/libctype.c ../../count-chars.c
 *     $ ./abs-demo
 *
 *   the paired MIPS assembly, in spimulator:
 *     $ spimulator -f ../libctype/libctype.asm -f ../libstdlib/libstdlib.asm -f abs-demo.asm
 *
 *   the same assembly, in the Python port:
 *     $ spimulator-python -f ../libctype/libctype.asm -f ../libstdlib/libstdlib.asm -f abs-demo.asm
 */
/* clang-format on */

#include "io.h"
#include "libstdlib.h"

static const int cases[] = {
    0,           1, -1, 100, -100, 2147483647, /* INT_MAX */
    -2147483647, /* INT_MAX flipped — abs returns +INT_MAX */
    -2147483648, /* INT_MIN — abs returns INT_MIN (UB in C; OK on two's
                    complement) */
};

#define N_CASES ((int)(sizeof cases / sizeof cases[0]))

__attribute__((noreturn)) void _start(void) {
  for (int i = 0; i < N_CASES; i++) {
    print_string("absolute(");
    print_int(cases[i]);
    print_string(") = ");
    print_int(absolute(cases[i]));
    print_string("    labsolute(");
    print_int((long)cases[i]);
    print_string(") = ");
    print_int((int)labsolute((long)cases[i]));
    print_char('\n');
  }
  os_exit(0);
}
