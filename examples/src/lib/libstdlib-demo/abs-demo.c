/* abs-demo — exercise libstdlib's abs + labs over representative
 * int values, including the INT_MIN edge case where -INT_MIN
 * overflows back to INT_MIN.
 *
 * Same pattern as atoi-demo: table of inputs, loop, print one
 * line per case.  Output is the pinned golden (abs-demo.expected).
 */

#include "io.h"
#include "libstdlib.h"

static const int cases[] = {
    0,
    1,
    -1,
    100,
    -100,
    2147483647,    /* INT_MAX */
    -2147483647,   /* INT_MAX flipped — abs returns +INT_MAX */
    -2147483648,   /* INT_MIN — abs returns INT_MIN (UB in C; OK on two's complement) */
};

#define N_CASES ((int)(sizeof cases / sizeof cases[0]))

__attribute__((noreturn)) void _start(void) {
  for (int i = 0; i < N_CASES; i++) {
    print_string("abs(");
    print_int(cases[i]);
    print_string(") = ");
    print_int(abs(cases[i]));
    print_string("    labs(");
    print_int((long)cases[i]);
    print_string(") = ");
    print_int((int)labs((long)cases[i]));
    print_char('\n');
  }
  os_exit(0);
}
