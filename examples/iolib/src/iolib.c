#include <inttypes.h>
#include <stdio.h>

void print_char(char c) { putchar(c); }

char read_char() { return getchar(); }

void print_int(int32_t i) { printf("%d", i); }

int32_t read_int() {
  int32_t toRead;
  scanf("%d", &toRead);
  return toRead;
}

void prfloat_float(float f) { printf("%f", f); }

float read_float() {
  float toRead;
  scanf("%f", &toRead);
  return toRead;
}
