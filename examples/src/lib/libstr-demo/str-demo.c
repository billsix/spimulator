/* str-demo — exercise every libstr function over a fixed set of
 * hardcoded subcases.  One line per subcase:
 *
 *     strcmp_eq=PASS
 *     memmove_bwd_overlap=PASS
 *
 * Each subcase computes a boolean from a libstr call and its
 * known-correct expected value, then prints `name=PASS` (or
 * `name=FAIL` if the library were ever wrong).  Because libstr.c
 * and libstr.asm implement the SAME algorithms correctly, every
 * subcase prints PASS and the C and asm outputs are byte-for-byte
 * identical — this file's output IS the golden (str-demo.expected).
 *
 * Coverage: equal / unequal / prefix string compares, bounded
 * compares, copy + bounded copy with NUL padding, forward and
 * reverse byte search, bounded memory search, sized memcpy,
 * memset fill and clear, and memmove with forward-overlap,
 * backward-overlap, and non-overlapping regions.
 */

#include "io.h"
#include "libstr.h"

/* Print "<name>=PASS\n" or "<name>=FAIL\n". */
static void report(const char* name, int ok) {
  print_string(name);
  print_string(ok ? "=PASS\n" : "=FAIL\n");
}

__attribute__((noreturn)) void _start(void) {
  char buf[16];

  /* ---- strlen: byte loop to the NUL sentinel ---- */
  report("strlen_empty", strlen("") == 0);
  report("strlen_hello", strlen("hello") == 5);

  /* ---- strcmp: three-way byte compare ---- */
  report("strcmp_eq", strcmp("abc", "abc") == 0);
  report("strcmp_lt", strcmp("abc", "abd") < 0);
  report("strcmp_gt", strcmp("abd", "abc") > 0);
  report("strcmp_prefix", strcmp("abc", "ab") > 0); /* longer > its prefix */

  /* ---- strncmp: compare bounded by a count ---- */
  report("strncmp_eq", strncmp("abcXX", "abcYY", 3) == 0); /* first 3 match */
  report("strncmp_diff", strncmp("abc", "abd", 3) < 0);

  /* ---- strcpy: copy including the NUL ---- */
  strcpy(buf, "hello");
  report("strcpy_basic", strcmp(buf, "hello") == 0);

  /* ---- strncpy: bounded copy, NUL-padded ---- */
  strncpy(buf, "hi", 5); /* -> 'h','i','\0','\0','\0' */
  report("strncpy_pad", buf[0] == 'h' && buf[1] == 'i' && buf[2] == '\0' &&
                            buf[3] == '\0' && buf[4] == '\0');
  strncpy(buf, "hello", 3); /* -> 'h','e','l' with NO terminator */
  report("strncpy_trunc", buf[0] == 'h' && buf[1] == 'e' && buf[2] == 'l');

  /* ---- strchr: pointer-returning byte search (NUL matches) ---- */
  const char* s = "hello";
  report("strchr_found", strchr(s, 'l') == s + 2); /* first 'l' */
  report("strchr_notfound", strchr(s, 'z') == NULL);
  report("strchr_nul", strchr(s, '\0') == s + 5); /* the terminator */

  /* ---- memchr: bounded byte search ---- */
  const char* m = "hello";
  report("memchr_found", memchr(m, 'l', 5) == m + 2);
  report("memchr_notfound", memchr(m, 'z', 5) == NULL);
  report("memchr_bounded", memchr(m, 'o', 3) == NULL); /* 'o' is past n=3 */

  /* ---- memcpy: count-driven copy ---- */
  memcpy(buf, "abcdef", 6);
  report("memcpy_basic", buf[0] == 'a' && buf[3] == 'd' && buf[5] == 'f');
  memcpy(buf, "ZZ", 0); /* n=0 copies nothing */
  report("memcpy_size0", buf[0] == 'a');

  /* ---- memset: count-driven fill ---- */
  memset(buf, 'x', 4);
  report("memset_fill",
         buf[0] == 'x' && buf[1] == 'x' && buf[2] == 'x' && buf[3] == 'x');
  memset(buf, 0, 3);
  report("memset_zero", buf[0] == 0 && buf[1] == 0 && buf[2] == 0);

  /* ---- memmove: overlap-aware copy ---- */
  char fwd[8] = {'a', 'b', 'c', 'd', 'e', 'f', 0, 0};
  memmove(fwd, fwd + 1, 4); /* dst<src, forward -> "bcdeef" */
  report("memmove_fwd_overlap", fwd[0] == 'b' && fwd[1] == 'c' &&
                                    fwd[2] == 'd' && fwd[3] == 'e' &&
                                    fwd[4] == 'e' && fwd[5] == 'f');
  char bwd[8] = {'a', 'b', 'c', 'd', 'e', 'f', 0, 0};
  memmove(bwd + 1, bwd, 4); /* dst>src, backward -> "aabcdf" */
  report("memmove_bwd_overlap", bwd[0] == 'a' && bwd[1] == 'a' &&
                                    bwd[2] == 'b' && bwd[3] == 'c' &&
                                    bwd[4] == 'd' && bwd[5] == 'f');
  char dst2[8];
  memmove(dst2, "xyz", 4); /* non-overlapping -> "xyz\0" */
  report("memmove_nonoverlap",
         dst2[0] == 'x' && dst2[1] == 'y' && dst2[2] == 'z' && dst2[3] == '\0');

  os_exit(0);
}
