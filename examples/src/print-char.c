/* PURPOSE: Write a single byte to stdout.
 *
 * One os_write call on one byte.  The spim equivalent is
 * `li $v0, 11 ; move $a0, c ; syscall`.
 */

#include "io.h"

void print_char(char c) { os_write(STDOUT, &c, 1); }
