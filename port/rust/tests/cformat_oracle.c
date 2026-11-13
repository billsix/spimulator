/* The C engine's five float formats, as glibc prints them: the oracle of
   tests/cformat_vs_c.rs.  Reads hex bit patterns of doubles, one per line. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  uint64_t w;
  double d;
  while (scanf("%" SCNx64, &w) == 1) {
    memcpy(&d, &w, 8);
    printf("%.18g %.8f %.17g %g [%#-13.6g]\n", d, d, d, d, d);
  }
}
