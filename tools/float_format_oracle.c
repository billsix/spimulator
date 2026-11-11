/* glibc's printf, the oracle for the ports' float formatters: read 64-bit
   double bit patterns in hex, one per line, and print each value in the five
   formats the C engine uses.  tools/check_float_format.py compiles and runs it;
   port/haskell/tests/cfmt.c is the same program. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  uint64_t w; double d;
  while (scanf("%" SCNx64, &w) == 1) { memcpy(&d, &w, 8); printf("%.18g %.8f %.17g %g [%#-13.6g]\n", d, d, d, d, d); }
}
