/* atoi-demo — exercise libstdlib's atoi over a representative
 * input set covering the interesting cases (positive, negative,
 * leading whitespace, leading '+', stops at first non-digit,
 * empty input, all-non-digit input).
 *
 * Output is deterministic and is pinned as atoi-demo.expected
 * so the asm-side run under spim can diff against the same
 * golden.
 */

#include "io.h"
#include "libstdlib.h"

static const char* cases[] = {
    "42",
    "-42",
    "+42",
    "0",
    "   17",       /* leading whitespace */
    "  -8",        /* whitespace then sign */
    "123abc",      /* digits then garbage — stops at 'a' */
    "abc",         /* no leading digits at all -> 0 */
    "",            /* empty -> 0 */
    "-2147483648", /* INT_MIN — the overflow-safe-accumulator test */
    "2147483647",  /* INT_MAX */
    " \t \n  99",  /* multi-whitespace including tab and newline */
};

#define N_CASES ((int)(sizeof cases / sizeof cases[0]))

__attribute__((noreturn)) void _start(void) {
  for (int i = 0; i < N_CASES; i++) {
    print_string("atoi(\"");
    print_string(cases[i]);
    print_string("\") = ");
    print_int(atoi(cases[i]));
    print_char('\n');
  }
  os_exit(0);
}
