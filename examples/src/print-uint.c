/* PURPOSE: Write the decimal representation of an UNSIGNED 32-bit
 *          int to stdout.  Mirrors print_int but skips the
 *          sign-handling so values > INT_MAX render correctly
 *          (e.g. CRC32 values, which routinely exceed 2^31).
 *
 *          The spim equivalent of this function is the same as
 *          for print_int — `li $v0, 1 ; move $a0, n ; syscall`
 *          — but spim's print_int syscall is signed too, so a
 *          spim demo that needs unsigned printing has to do the
 *          digit conversion by hand (see cksum.asm).
 */

#include "io.h"

void print_uint(unsigned int value) {
    char buf[16];
    int pos = (int)sizeof(buf);

    do {
        pos--;
        buf[pos] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0);

    os_write(STDOUT, &buf[pos], (size_t)((int)sizeof(buf) - pos));
}
