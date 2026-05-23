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
