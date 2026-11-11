/* Print the bits of atof() of each input line, in hex (check-strtod.sh). */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  char line[512];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\n")] = '\0';
    double d = atof(line);
    uint64_t w;
    memcpy(&w, &d, 8);
    printf("%" PRIx64 "\n", w);
  }
}
