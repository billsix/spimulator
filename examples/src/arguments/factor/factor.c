// Copyright (c) 2021-2026 William Emerison Six (MIT)

/* PURPOSE: A simplified `factor N` — print N's prime factors.
 *
 *          Invocation:
 *              factor N
 *
 *          Output format (matches real `factor`):
 *              factor 360
 *              360: 2 2 2 3 3 5
 *
 *          Trial-divide algorithm: for d = 2, 3, 4, ... up to
 *          sqrt(N), while N is divisible by d, print d and
 *          divide.  Any leftover N > 1 is prime; print it.
 *
 *          Pairs naturally with sieve (also primes).  No
 *          input I/O — pure argv → algorithm → stdout.
 */

#include "io.h"
#include "crt0.h"

int my_main(int argc, char **argv) {
  if (argc != 2) {
    print_string("usage: factor N\n");
    return 1;
  }
  int n = parse_int(argv[1]);
  if (n < 1) {
    print_string("factor: N must be >= 1\n");
    return 1;
  }

  print_int(n);
  print_char(':');

  if (n == 1) {
    print_char('\n');
    return 0;
  }

  for (int d = 2; d * d <= n; d++) {
    while (n % d == 0) {
      print_char(' ');
      print_int(d);
      n /= d;
    }
  }
  if (n > 1) {
    print_char(' ');
    print_int(n);
  }
  print_char('\n');
  return 0;
}
