/* bsearch-demo — exercise libstdlib's bsearch over a sorted
 * int array.  Searches several keys: some present (including
 * boundaries), some absent (including below/above the range).
 *
 * Output reports the INDEX of the match rather than the raw
 * pointer, so the C version and the spim version agree.
 *
 * Same pattern as the other libstdlib demos: deterministic
 * stdout, pinned as bsearch-demo.expected.
 */

#include "io.h"
#include "libstdlib.h"

static const int data[] = {5, 12, 23, 34, 47, 56, 67, 78, 89, 100};
#define N ((unsigned)(sizeof data / sizeof data[0]))

/* Comparator on ints — the classic `*(int*)a - *(int*)b` form.
 * Beware of overflow on very large/very negative values; safe
 * for the demo's modest ints. */
static int int_cmp(const void* a, const void* b) {
  int ia = *(const int*)a;
  int ib = *(const int*)b;
  return ia - ib;
}

static const int keys[] = {
    5,   /* first element */
    47,  /* middle, present */
    100, /* last element */
    23,  /* present, near front */
    89,  /* present, near back */
    50,  /* between 47 and 56 — absent */
    0,   /* below the range */
    200, /* above the range */
    13,  /* between 12 and 23 — absent */
};
#define N_KEYS ((int)(sizeof keys / sizeof keys[0]))

__attribute__((noreturn)) void _start(void) {
  for (int i = 0; i < N_KEYS; i++) {
    int key = keys[i];
    int* found = (int*)bsearch(&key, data, N, sizeof(int), int_cmp);
    print_string("bsearch(");
    print_int(key);
    print_string(") = ");
    if (found) {
      print_string("idx ");
      print_int((int)(found - data));
    } else {
      print_string("not found");
    }
    print_char('\n');
  }
  os_exit(0);
}
